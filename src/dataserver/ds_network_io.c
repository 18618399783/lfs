/**
*
*
*
*
*
**/

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/sendfile.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <event.h>
#include <pthread.h>

#include "logger.h"
#include "shared_func.h"
#include "sock_opt.h"
#include "ds_types.h"
#include "ds_func.h"
#include "ds_queue.h"
#include "ds_conn.h"
#include "ds_file.h"
#include "ds_service.h"
#include "ds_disk_io.h"
#include "ds_network_io.h"

#define MAX_PACKAGE_DATA_SIZE (8 * 1024)

static int nio_init_threads_count = 0;
static int last_nio_tid = 0;
static pthread_mutex_t nio_threads_lock;
static pthread_cond_t nio_threads_cond;

static libevent_thread *nio_tids = NULL;
libevent_dispatch_thread dispatch_thread;
volatile int do_run_nio_main_thread = 1;
volatile int do_run_nio_work_thread = 1;

static void __on_accept(int fd,short ev,void *arg);
static void __wait_for_nio_thread_startup(int nthreads);
//static void __worker_thread_reg_cond(void);	

static void __state_machine(conn *c);

static int __setup_nio_thread(libevent_thread *thread);
static void __libevent_thread_callback(int fd,short which,void *arg);
static void __event_handler_callback(const int fd,short which,void *arg);
static void* __nio_thread_entry(void *arg);
static void __nio_thread_init_register(void);
static void __create_nio_thread(void* (*func)(void*),void *arg);
static void __dispatch_conn(int sfd);

static enum network_read_result __network_read_header(conn *c);
static enum network_read_result __network_read_body(conn *c);
static enum network_read_result __network_read(conn *c);
static enum network_write_result __network_write_noblock(conn *c);
static enum network_read_result __network_fread(conn *c);
static enum network_write_result __network_fwrite(conn *c);
static enum network_write_result __network_fsend(conn *c);

static int __parse_protocol_cmd(conn *c);
static void dispatch_cmd(conn *c,uint8_t cmd);

int nio_threads_init(int nthreads,struct event_base *main_base)
{
	int i = 0;
	int ret = LFS_OK;

	pthread_mutex_init(&nio_threads_lock,NULL);
	pthread_cond_init(&nio_threads_cond,NULL);

	nio_tids = calloc(nthreads,sizeof(libevent_thread)); 
	if(!nio_tids)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"Allocate work thread failed!", __LINE__);
		return LFS_ERROR;
	}

	dispatch_thread.base = main_base;
	dispatch_thread.thread_id = pthread_self();

	for(i = 0;i < nthreads;i++)
	{
		int fds[2];
		if(pipe(fds))
		{
			logger_error("file: "__FILE__", line: %d, " \
						"Can't create notify pipe!", __LINE__);
			return LFS_ERROR;
		}
		//set_noblock(fds[0]);
		//set_noblock(fds[1]);
		nio_tids[i].notify_receive_fd = fds[0];
		nio_tids[i].notify_send_fd = fds[1];
		__setup_nio_thread(&nio_tids[i]);
		ctxs.reserved_fds += 5;
	}
	for(i = 0;i < nthreads; i++)
	{
		__create_nio_thread(__nio_thread_entry,&nio_tids[i]);
	}
	pthread_mutex_lock(&nio_threads_lock);
	__wait_for_nio_thread_startup(nthreads);
	pthread_mutex_unlock(&nio_threads_lock);
	return ret;
}

void nio_threads_destroy()
{
	pthread_mutex_destroy(&nio_threads_lock);
	pthread_cond_destroy(&nio_threads_cond);
	do_run_nio_work_thread = 0;
	if(nio_tids != NULL)
	{
		free(nio_tids);
		nio_tids = NULL;
	}
}

void nio_dispatch(int sfd)
{
	struct event ev_accept;

	event_set(&ev_accept,sfd,EV_READ|EV_PERSIST,__on_accept,NULL);
	event_base_set(dispatch_thread.base,&ev_accept);
	if(event_add(&ev_accept,0) == -1)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"Can't moniter main dispatch accept event!", __LINE__);
		return;
	}
	event_base_loop(dispatch_thread.base,0);
}

void nio_accept_loop(int sfd)
{
	int insfd;
	struct sockaddr_in inaddr;
	socklen_t inaddr_len;
	
	while(do_run_nio_main_thread)
	{
		inaddr_len = sizeof(inaddr);
		insfd = accept(sfd,(struct sockaddr*)&inaddr,&inaddr_len);
	   if(insfd < 0)
	   {
		   if(errno == EMFILE)
		   {
				logger_error("file: "__FILE__", line: %d, " \
						"Too many open connections,errno:%d,"\
						"error info:%s!", __LINE__,errno,strerror(errno));
		   }
		   else if(!((errno == EAGAIN) || errno == EWOULDBLOCK))
		   {
				logger_error("file: "__FILE__", line: %d, " \
						"Failed to accept,errno:%d,error info:" \
						"%s!", __LINE__,errno,strerror(errno));
		   }
	   }	   
		else
			__dispatch_conn(insfd);
	}
}

static void __on_accept(int fd,short ev,void *arg)
{
	int cfd;
	struct sockaddr_in inaddr;
	socklen_t inaddr_len;

	inaddr_len = sizeof(inaddr);
	cfd = accept(fd,(struct sockaddr*)&inaddr,&inaddr_len);

	if(cfd > 0)
	{
		if(ctxs.curr_conns + ctxs.reserved_fds >= confitems.max_connections - 1)
		{
			logger_crit("file: "__FILE__", line: %d, " \
					"Too many open connections!", __LINE__);
			close(cfd);
			CTXS_LOCK();
			ctxs.rejected_conns++;
			CTXS_UNLOCK();
			return;
		}
		if(set_noblock(cfd) != 0)
		{
			logger_warning("file: "__FILE__", line: %d, " \
					"Failed to setting noblock!", __LINE__);
		}
		__dispatch_conn(cfd);
	}
	if(cfd < 0)
	{
		if(errno == EMFILE)
		{
			logger_crit("file: "__FILE__", line: %d, " \
					"Too many open connections,errno:%d,"\
					"error info:%s!", __LINE__,errno,strerror(errno));
			return;
		}
		else if(!((errno == EAGAIN) || errno == EWOULDBLOCK))
		{
			logger_error("file: "__FILE__", line: %d, " \
					"Failed to accept,errno:%d,error info:"\
					"%s!", __LINE__,errno,strerror(errno));
			return;
		}
	}	   
}

static void __wait_for_nio_thread_startup(int nthreads)
{
	while(nio_init_threads_count < nthreads)
	{
		pthread_cond_wait(&nio_threads_cond,&nio_threads_lock);
	}
}

/*
static void __worker_thread_reg_cond(void)
{
	pthread_mutex_lock(&nio_threads_lock);
	nio_init_threads_count++;
	pthread_cond_signal(&nio_threads_cond);
	pthread_mutex_unlock(&nio_threads_lock);
}
*/

static int __setup_nio_thread(libevent_thread *thread)
{
	//thread->base = event_init();
	thread->base = event_base_new();
	if(!thread->base)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"Can't create libevent base!", __LINE__);
		return LFS_ERROR;
	}
	event_set(&thread->notify_event,thread->notify_receive_fd,EV_READ|EV_PERSIST,__libevent_thread_callback,thread);
	event_base_set(thread->base,&thread->notify_event);
	if(event_add(&thread->notify_event,0) == -1)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"Can't monitor libevent notify pipe!", __LINE__);
		return -2;
	}

	init_queue(&(thread->ciq));
	init_queue(&(thread->cq));
	return LFS_OK;
}

static void __libevent_thread_callback(int fd,short which,void *arg)
{
	libevent_thread *thread = (libevent_thread*)arg;
	conn_item *conn_item = NULL;
	char buf[1];

	if(read(fd,buf,1) != 1)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"Can't read from libevent pipe!", __LINE__);
	}
	switch(buf[0])
	{
		case 'c':
			conn_item = conn_item_queue_pop(&(thread->ciq));
			if(conn_item)
			{
				conn *c = NULL;
				c = conn_new(conn_item->sfd,conn_item->init_state,confitems.nio_read_buffer_size,confitems.nio_write_buffer_size,conn_item->event_flags,__event_handler_callback,thread->base);
				if(!c)
				{
					logger_error("file: "__FILE__", line: %d, " \
								"Can't listen for events on socket!", __LINE__);
					close(conn_item->sfd);
					CTXS_LOCK();
					ctxs.curr_conns--;
					CTXS_UNLOCK();
				}
				else
				{
					c->libevent_thread = thread;
				}
				conn_item_free(conn_item);	
			}
			break;
		case 'd':
			do{
				conn *c = NULL;
				c = conn_queue_pop(&(thread->cq));
				assert(c != NULL);
				add_conn_event(c,c->event_flags,__event_handler_callback,thread->base);
			}while(0);
			break;
		default:
			break;
	}
	return;
}

static void __event_handler_callback(const int fd,short which,void *arg)
{
	conn *c = NULL;
	c = (conn*)arg;
	if(!c) return;

	c->which = which;
	if(fd != c->sfd)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"Event fd doesn't match conn fd!", __LINE__);
		conn_close(c);
		return;
	}	
	__state_machine(c);
}

static void __state_machine(conn *c)
{
	assert(c != NULL);
	bool flag = false;
	int ret;


	while(!flag)
	{
		switch(c->state)
		{
		case conn_waiting:
			if(update_conn_event(c,EV_READ | EV_PERSIST,__event_handler_callback) != 0)
			{
				logger_error("file: "__FILE__", line: %d, " \
							"Failed to update connection object event!", __LINE__);
				set_conn_state(c,conn_closed);
			}
			set_conn_state(c,conn_read);
			flag = true;
			break;
		case conn_read:
			ret = __network_read(c);
			switch(ret){
			case READ_NO_DATA_RECEIVED:
				set_conn_state(c,conn_waiting);
				break;
			case READ_DATA_RECEIVED:
				set_conn_state(c,conn_parse_cmd);
				break;
			case READ_HARD_ERROR:
				set_conn_state(c,conn_closed);
				break;
			case READ_SOFT_ERROR:
				set_conn_state(c,conn_closed);
				break;
			}
			break;
		case conn_fread:
			ret = __network_fread(c);
			switch(ret){
			case READ_COMPLETE:
				set_conn_state(c,conn_n2dio);
				break;
			case READ_INCOMPLETE:
				flag = true;
				break;
			case READ_HARD_ERROR:
				set_conn_state(c,conn_closed);
				break;
			case READ_SOFT_ERROR:
				set_conn_state(c,c->nextto);
				break;
			}
			break;
		case conn_parse_cmd:
			if(__parse_protocol_cmd(c) == 0)
				set_conn_state(c,conn_waiting);
			break;
		case conn_write:
			ret = __network_write_noblock(c);
			switch(ret){
			case WRITE_SUCCESS:
				conn_reset(c);
				set_conn_state(c,conn_waiting);
			case WRITE_COMPLETE:
				conn_reset(c);
				set_conn_state(c,conn_waiting);
				break;
			case WRITE_INCOMPLETE:
				flag = true;
				break;
			case WRITE_HARD_ERROR:
				set_conn_state(c,conn_closed);
				break;
			case WRITE_SOFT_ERROR:
				set_conn_state(c,conn_closed);
				break;
			}
			break;
		case conn_fwrite:
			ret = __network_fwrite(c);
			switch(ret){
			case WRITE_COMPLETE:
				conn_reset(c);
				set_conn_state(c,conn_n2dio);
				break;
			case WRITE_INCOMPLETE:
				flag = true;
				break;
			case WRITE_HARD_ERROR:
				set_conn_state(c,conn_closed);
				break;
			case WRITE_SOFT_ERROR:
				set_conn_state(c,conn_closed);
				break;
			}
			break;
		case conn_fsend:
			ret = __network_fsend(c);
			switch(ret){
			case WRITE_COMPLETE:
				conn_reset(c);
				set_conn_state(c,conn_waiting);
				break;
			case WRITE_HARD_ERROR:
				set_conn_state(c,conn_closed);
				break;
			case WRITE_SOFT_ERROR:
				set_conn_state(c,conn_closed);
				break;
			}
			break;
		case conn_mwrite:
			ret = __network_write_noblock(c);
			switch(ret){
			case WRITE_SUCCESS:
				conn_write_reset(c);
				set_conn_state(c,c->nextto);
			case WRITE_COMPLETE:
				conn_write_reset(c);
				set_conn_state(c,c->nextto);
				break;
			case WRITE_INCOMPLETE:
				flag = true;
				break;
			case WRITE_HARD_ERROR:
				set_conn_state(c,conn_closed);
				break;
			case WRITE_SOFT_ERROR:
				set_conn_state(c,conn_closed);
				break;
			}
			break;
		case conn_n2dio:
			do{
				if((ret = dio_thread_pool_dispatch(c)) != 0)
				{
					set_conn_state(c,conn_closed);
					break;
				}
				flag = true;
			}while(0);
			break;
		case conn_closed:
			conn_close(c);
			flag = true;
			break;
		}
	}
	return;
}


static void* __nio_thread_entry(void *arg)
{
	libevent_thread *thread = (libevent_thread*)arg;	
	__nio_thread_init_register();
	while(do_run_nio_work_thread)
	{
		event_base_loop(thread->base,0);
	}
	event_base_free(thread->base);
	return NULL;
}

static void __nio_thread_init_register(void)
{
	pthread_mutex_lock(&nio_threads_lock);
	nio_init_threads_count++;
	pthread_cond_signal(&nio_threads_cond);
	pthread_mutex_unlock(&nio_threads_lock);
}

static void __create_nio_thread(void* (*func)(void*),void *arg)
{
	pthread_t thread;
	pthread_attr_t attr;
	libevent_thread *worker_thread = (libevent_thread*)arg;	
	int ret;

	//pthread_attr_init(&attr);

	if((ret = init_pthread_attr(&attr,confitems.thread_stack_size)) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"Failed init pthread attr: %s", __LINE__,strerror(ret));
		return;
	}


	if((ret = pthread_create(&thread,&attr,func,arg)) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"Can't create nio thread,errno:%d,error info:%s.",\
				   	__LINE__,\
					errno,\
					strerror(ret));
		return;
	}
	worker_thread->thread_id = pthread_self();
	pthread_attr_destroy(&attr);
}

static void __dispatch_conn(int sfd)
{
	char buf[1];
	conn_item *item = conn_item_new();
	if(item == NULL)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"Failed to allocate for connection object!", __LINE__);
		close(sfd);
		return;
	}
	pthread_mutex_lock(&nio_threads_lock);
	int tid = (last_nio_tid + 1) % confitems.nio_threads;
	libevent_thread *thread = nio_tids + tid;
	last_nio_tid = tid;
	pthread_mutex_unlock(&nio_threads_lock);

	item->sfd = sfd;
	item->init_state = conn_read;
	item->event_flags = EV_READ|EV_PERSIST;

	conn_item_queue_push(&thread->ciq,item);

	CTXS_LOCK();
	ctxs.curr_conns++;
	ctxs.total_conns++;
	CTXS_UNLOCK();

	buf[0] = 'c';
	if(write(thread->notify_send_fd,buf,1) != 1)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"Writing to thread notify pipe!", __LINE__);
	}
}

static enum network_read_result __network_read_header(conn *c)
{
	assert(c != NULL);
	enum network_read_result read_state = READ_NO_DATA_RECEIVED; 
	int res;

	while(1)
	{
		int avail = sizeof(protocol_header);
		res = read(c->sfd,c->rcurr + c->rbytes,avail);
		if(res > 0)
		{
			c->rbytes += res;
			if(c->rbytes == sizeof(protocol_header))
			{
				read_state = READ_DATA_RECEIVED;
				break;
			}
		}		
		if(res == 0)
		{
			logger_warning("file: "__FILE__", line: %d, "\
					"The client has closed connection,errno:%d,error info:%s!"\
					, __LINE__,errno,strerror(errno));
			return READ_HARD_ERROR;
		}
		if(res == -1)
		{
			if(errno == EAGAIN || errno == EWOULDBLOCK)
				break;
			else
			{
				logger_error("file: "__FILE__", line: %d, " \
						"Failed to read socket,errno:%d" \
						",error info:%s!", __LINE__,errno,strerror(errno));
				return READ_HARD_ERROR;
			}
		}
	}
	return read_state;
}


static enum network_read_result __network_read_body(conn *c)
{
	assert(c != NULL);
	enum network_read_result read_state = READ_NO_DATA_RECEIVED; 
	int res;
	int64_t body_len = 0;
	protocol_header *procl_header;

	procl_header = (protocol_header*)c->rbuff;
	body_len = procl_header->header_s.body_len;

	if(body_len > MAX_PACKAGE_DATA_SIZE)
	{
		handle_protocol_error(c,PROTOCOL_RESP_STATUS_ERROR_PACKAGESIZE_OVERFLOW);
		return READ_SOFT_ERROR;
	}

	while(1)
	{
		if(c->rbytes >= c->rsize)
		{
			if(c->alloc_count == 2)
				return read_state;
			c->alloc_count++;
			char *realloc_buff = realloc(c->rbuff,c->rsize * 2);
		   if(NULL == realloc_buff)
		   {
			   c->rbytes = 0;
			   logger_error("file: "__FILE__", line: %d, " \
							"Out of memory reading request!", __LINE__);
			   handle_protocol_error(c,PROTOCOL_RESP_STATUS_ERROR_MEMORY);
			   return READ_SOFT_ERROR;
		   }	   
		   c->rcurr = c->rbuff = realloc_buff;
		   c->rsize *= 2;
		}
		int avail = body_len - (c->rbytes - sizeof(protocol_header));
		res = read(c->sfd,c->rcurr + c->rbytes,avail);
		if(res > 0)
		{
			c->rbytes += res;
			if(c->rbytes == (body_len + sizeof(protocol_header)))
			{
				read_state = READ_DATA_RECEIVED;
				break;
			}
		}		
		if(res == 0)
		{
			logger_warning("file: "__FILE__", line: %d, "\
					"The client has closed connection,errno:%d,error info:%s!"\
					, __LINE__,errno,strerror(errno));
			return READ_HARD_ERROR;
		}
		if(res == -1)
		{
			if(errno == EAGAIN || errno == EWOULDBLOCK)
				break;
			else
			{
				logger_error("file: "__FILE__", line: %d, " \
						"Failed to read socket,errno:%d" \
						",error info:%s!", __LINE__,errno,strerror(errno));
				return READ_HARD_ERROR;
			}
		}
	}

	return read_state;
}

static enum network_read_result __network_read(conn *c)
{
	assert(c != NULL);
	enum network_read_result read_state = READ_NO_DATA_RECEIVED; 

	if(c->rbytes < sizeof(protocol_header))
	{
		return __network_read_header(c);
	}
	read_state = __network_read_body(c);
	return read_state;
}

static enum network_read_result __network_fread(conn *c)
{
	assert(c != NULL);
	assert(c->fctx != NULL);
	int res;
	enum network_read_result read_state = READ_COMPLETE; 

	while(1)
	{
		if((c->fctx->f_rwoffset >= c->fctx->f_size) || \
				(c->fctx->f_buff_offset >= c->fctx->f_buff_size))
		{
			return READ_COMPLETE;
		}
		int avail = c->fctx->f_buff_size - c->fctx->f_buff_offset;
		res = read(c->sfd,c->fctx->f_buff + c->fctx->f_buff_offset,avail);
		if(res > 0)
		{
			c->fctx->f_buff_offset += res;
			c->fctx->f_rwoffset += res;
			if(res == avail)
				continue;
			else
				break;
		}		
		if(res == 0)
		{
			return READ_HARD_ERROR;
		}
		if((res == -1) && (errno == EAGAIN || errno == EWOULDBLOCK))
		{
			if(update_conn_event(c,EV_READ | EV_PERSIST,__event_handler_callback) != 0)
			{
				logger_error("file: "__FILE__", line: %d, " \
							"Failed to update connection object event!", __LINE__);
				return READ_HARD_ERROR;
			}
			return READ_INCOMPLETE;
		}
	}
	return read_state;
}

static enum network_write_result __network_write_noblock(conn *c)
{
	assert(c != NULL);
	int written_bytes = 0;
	int remain_bytes = 0;
	
	while(1)
	{
		remain_bytes = c->wbytes - c->wused;
		c->wcurr = c->wbuff + c->wused;
		if(remain_bytes > 0)
		{
			written_bytes = write(c->sfd,c->wcurr,remain_bytes);
			if(written_bytes > 0)
			{
				c->wused += written_bytes;
				continue;
			}
			if((written_bytes == -1) && (errno == EINTR || errno == EWOULDBLOCK))
			{
				if(update_conn_event(c,EV_WRITE | EV_PERSIST,__event_handler_callback) != 0)
				{
					logger_error("file: "__FILE__", line: %d, " \
							"Failed to update connection object event!", __LINE__);
					return WRITE_SOFT_ERROR;
				}
				return WRITE_INCOMPLETE;
			}
			else
			{
				return WRITE_HARD_ERROR;
			}
		}
		else
		{
			return WRITE_COMPLETE;
		}
	}
	return WRITE_COMPLETE;
}

static enum network_write_result __network_fwrite(conn *c)
{
	assert(c != NULL);
	int written_bytes = 0;
	int remain_bytes = 0;
	
	while(1)
	{
		remain_bytes = c->fctx->f_buff_offset - c->fctx->f_rwoffset;
		if(remain_bytes > 0)
		{
			written_bytes = write(c->sfd,c->fctx->f_buff + c->fctx->f_rwoffset,remain_bytes);
			if(written_bytes > 0)
			{
				c->fctx->f_rwoffset += written_bytes;
				c->fctx->f_offset += written_bytes;
				c->fctx->f_total_offset += written_bytes;
				continue;
			}
			if((written_bytes == -1) && (errno == EINTR || errno == EWOULDBLOCK))
			{
				if(update_conn_event(c,EV_WRITE | EV_PERSIST,__event_handler_callback) != 0)
				{
					logger_error("file: "__FILE__", line: %d, " \
							"Failed to update connection object event!", __LINE__);
					return WRITE_SOFT_ERROR;
				}
				return WRITE_INCOMPLETE;
			}
			else
			{
				return WRITE_HARD_ERROR;
			}
		}
		else
		{
			return WRITE_COMPLETE;
		}
	}
	return WRITE_COMPLETE;
}

static enum network_write_result __network_fsend(conn *c)
{
	assert((c != NULL) && (c->fctx != NULL));
	int fd;
	int64_t send_bytes;
	int64_t offset;
	int64_t remain_bytes;
	int flags;

	fd = open(c->fctx->f_path_name,O_RDONLY);
	if(fd < 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"Open file: %s failed, errno:%d,error info: %s.",\
				   	__LINE__,c->fctx->f_path_name,errno,strerror(errno));
		return WRITE_HARD_ERROR;
	}
	flags = fcntl(c->sfd,F_GETFL,0);
	if(flags < 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"Open file: %s failed, errno:%d,error info: %s.",\
				   	__LINE__,c->fctx->f_path_name,errno,strerror(errno));
		close(fd);
		return WRITE_HARD_ERROR;
	}
	if(flags & O_NONBLOCK)
	{
		if(fcntl(c->sfd,F_SETFL,flags & ~O_NONBLOCK) == -1)
		{
			logger_error("file: "__FILE__", line: %d, " \
					"Setting properties file: %s failed, errno:%d,error info: %s.",\
					__LINE__,c->fctx->f_path_name,errno,strerror(errno));
			close(fd);
			return WRITE_HARD_ERROR;
		}
	}
	offset = c->fctx->f_offset;
	remain_bytes = c->fctx->f_size;
	while(remain_bytes > 0)
	{
		send_bytes = sendfile(c->sfd,fd,&offset,remain_bytes);
		if(send_bytes <= 0)
		{
			logger_error("file: "__FILE__", line: %d, " \
					"Send file: %s failed, errno:%d,error info: %s.",\
					__LINE__,c->fctx->f_path_name,errno,strerror(errno));
			break;
		}
		remain_bytes -= send_bytes;
	}
	if(flags & O_NONBLOCK)
	{
		if(fcntl(c->sfd,F_SETFL,flags) == -1)
		{
			logger_error("file: "__FILE__", line: %d, " \
					"Settingg properties file: %s failed, errno:%d,error info: %s.",\
					__LINE__,c->fctx->f_path_name,errno,strerror(errno));
			close(fd);
			return WRITE_HARD_ERROR;
		}
	}
	close(fd);
	return WRITE_COMPLETE;
}

static int __parse_protocol_cmd(conn *c)
{
	assert(c != NULL);
	assert(c->rbytes > 0);

	if(c->rbytes < sizeof(protocol_header))
		return 0;
	else
	{
		protocol_header *procl_header;
		procl_header = (protocol_header*)c->rbuff;
		
		if((c->rbytes - sizeof(procl_header->header_s)) >= procl_header->header_s.body_len)
		{
			c->rbytes -= sizeof(procl_header->header_s);
			c->rcurr = c->rbuff + sizeof(procl_header->header_s);
			dispatch_cmd(c,procl_header->header_s.cmd);
		}
		else
		{
			return 0;
		}
	}
	return 1;
}

static void dispatch_cmd(conn *c,uint8_t cmd)
{
	protocol_resp_status resp_status = PROTOCOL_RESP_STATUS_SUCCESS;
	switch(cmd)
	{
		case PROTOCOL_CMD_FILE_UPLOAD:
			resp_status = handle_cmd_fileupload(c);	
			break;
		case PROTOCOL_CMD_FILE_DOWNLOAD:
			resp_status = handle_cmd_filedownload(c);	
			break; 
		case PROTOCOL_CMD_ASYNC_COPY_FILE:
			resp_status = handle_cmd_asynccopyfile(c);
			break; 
		case PROTOCOL_CMD_FULL_SYNC_GET_MASTER_BINLOG_METE:
			resp_status = handle_cmd_getmasterbinlogmete(c);
			break; 
		case PROTOCOL_CMD_FULL_SYNC_COPY_MASTER_BINLOG:
			resp_status = handle_cmd_copymasterbinlog(c);
			break; 
		case PROTOCOL_CMD_FULL_SYNC_COPY_MASTER_DATA:
			resp_status = handle_cmd_copymasterdata(c);
			break; 
		default:
			logger_warning("file: "__FILE__", line: %d, " \
					"Ivalid cmd %02X!",\
					__LINE__,cmd);
			break;
	}

	if(resp_status)
		handle_protocol_error(c,resp_status);
	return;
}
