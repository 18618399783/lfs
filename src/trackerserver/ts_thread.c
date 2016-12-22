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
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <event.h>
#include <pthread.h>

#include "logger.h"
#include "shared_func.h"
#include "sock_opt.h"
#include "ts_func.h"
#include "ts_queue.h"
#include "ts_conn.h"
#include "ts_service.h"
#include "ts_thread.h"

#define MAX_PACKAGE_DATA_SIZE 8 * 1024

enum network_read_result{
	READ_DATA_RECEIVED,
	READ_NO_DATA_RECEIVED,
	READ_SOFT_ERROR,
	READ_HARD_ERROR
};	

enum network_write_result{
	WRITE_COMPLETE,
	WRITE_INCOMPLETE,
	WRITE_SOFT_ERROR,
	WRITE_HARD_ERROR
};

static int init_threads_count = 0;
static int last_tid = -1;
static pthread_mutex_t init_threads_lock;
static pthread_cond_t init_threads_cond;
volatile int do_work_thread = 1;

static libevent_thread *threads = NULL;

static void __on_accept(int fd,short ev,void *arg);
static void __wait_for_worker_thread_startup(int nthreads);
//static void __worker_thread_reg_cond(void);	

static void __state_machine(conn *c);

static int __setup_thread(libevent_thread *thread);
static void __libevent_thread_callback(int fd,short which,void *arg);
static void __event_handler_callback(const int fd,short which,void *arg);
static void* __thread_worker(void *arg);
static void __thread_init_register(void);
static void __create_worker_thread(void* (*func)(void*),void *arg);
static void __dispatch_conn(int sfd);

static enum network_read_result __network_read(conn *c);
static enum network_write_result __network_write_noblock(conn *c);
//static enum network_write_result __network_write_block(conn *c);

static int __parse_protocol_cmd(conn *c);
static void dispatch_cmd(conn *c,uint8_t cmd);

int ts_thread_init(int nthreads,struct event_base *main_base)
{
	int i = 0;
	int ret = 0;

	pthread_mutex_init(&init_threads_lock,NULL);
	pthread_cond_init(&init_threads_cond,NULL);

	threads = calloc(nthreads,sizeof(libevent_thread)); 
	if(!threads)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"Can't allocate threads descriptors!", __LINE__);
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
		threads[i].notify_receive_fd = fds[0];
		threads[i].notify_send_fd = fds[1];
		__setup_thread(&threads[i]);
		ctxs.reserved_fds += 5;
	}
	for(i = 0;i < nthreads; i++)
	{
		__create_worker_thread(__thread_worker,&threads[i]);
	}
	pthread_mutex_lock(&init_threads_lock);
	__wait_for_worker_thread_startup(nthreads);
	pthread_mutex_unlock(&init_threads_lock);
	return ret;
}

void ts_thread_destroy()
{
	pthread_mutex_destroy(&init_threads_lock);
	pthread_cond_destroy(&init_threads_cond);
	do_work_thread = 0;
}	

void ts_dispatch(int sfd)
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

void ts_accept_loop(int sfd)
{
	int insfd;
	struct sockaddr_in inaddr;
	socklen_t inaddr_len;
	
	while(main_thread_flag)
	{
		inaddr_len = sizeof(inaddr);
		insfd = accept(sfd,(struct sockaddr*)&inaddr,&inaddr_len);
	   if(insfd < 0)
	   {
		   if(errno == EMFILE)
		   {
				logger_error("file: "__FILE__", line: %d, " \
						"Too many open connections,errno:%d,\
						error info:%s!", __LINE__,errno,STRERROR(errno));
		   }
		   else if(!((errno == EAGAIN) || errno == EWOULDBLOCK))
		   {
				logger_error("file: "__FILE__", line: %d, " \
						"Failed to accept,errno:%d,error info:\
						%s!", __LINE__,errno,STRERROR(errno));
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
					"Too many open connections,errno:%d,\
					error info:%s!", __LINE__,errno,STRERROR(errno));
			return;
		}
		else if(!((errno == EAGAIN) || errno == EWOULDBLOCK))
		{
			logger_error("file: "__FILE__", line: %d, " \
					"Failed to accept,errno:%d,error info:\
					%s!", __LINE__,errno,STRERROR(errno));
			return;
		}
	}	   
}

static void __wait_for_worker_thread_startup(int nthreads)
{
	while(init_threads_count < nthreads)
	{
		pthread_cond_wait(&init_threads_cond,&init_threads_lock);
	}
}

/*
static void __worker_thread_reg_cond(void)
{
	pthread_mutex_lock(&init_threads_lock);
	init_threads_count++;
	pthread_cond_signal(&init_threads_cond);
	pthread_mutex_unlock(&init_threads_lock);
}
*/

static int __setup_thread(libevent_thread *thread)
{
	thread->base = event_init();
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
		return LFS_ERROR;
	}

	init_queue(&(thread->cq_head));
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
			conn_item = conn_item_queue_pop(&(thread->cq_head));
			if(conn_item)
			{
				conn *c = NULL;
				c = conn_new(conn_item->sfd,conn_item->init_state,\
						confitems.conn_buffer_size,conn_item->event_flags,__event_handler_callback,thread->base);
				if(!c)
				{
					logger_error("file: "__FILE__", line: %d, " \
								"Can't listen for events on socket!", __LINE__);
					close(conn_item->sfd);
				}
				else
				{
					c->libevent_thread = thread;
				}
				conn_item_free(conn_item);	
			}
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
	bool flag = false;
	int ret;
	if(NULL == c) return;

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
				break;
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
		case conn_nread:
			break;
		case conn_parse_cmd:
			if(__parse_protocol_cmd(c) == 0)
				set_conn_state(c,conn_waiting);
			break;
		case conn_write:
			ret = __network_write_noblock(c);
			switch(ret){
			case WRITE_COMPLETE:
				conn_reset(c);
				set_conn_state(c,c->writeto);
				break;
			case WRITE_INCOMPLETE:
				break;
			case WRITE_HARD_ERROR:
				set_conn_state(c,conn_closed);
				break;
			case WRITE_SOFT_ERROR:
				flag = true;
				break;
			}
			break;
		case conn_closed:
			conn_close(c);
			flag = true;
			break;
		}
	}
	return;
}


static void* __thread_worker(void *arg)
{
	libevent_thread *thread = (libevent_thread*)arg;	
	__thread_init_register();
	while(do_work_thread)
	{
		event_base_loop(thread->base,0);
	}
	event_base_free(thread->base);
	//event_base_dispatch(thread->base);
	return NULL;
}

static void __thread_init_register(void)
{
	pthread_mutex_lock(&init_threads_lock);
	init_threads_count++;
	pthread_cond_signal(&init_threads_cond);
	pthread_mutex_unlock(&init_threads_lock);
}

static void __create_worker_thread(void* (*func)(void*),void *arg)
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
					"Can't create thread: %s", __LINE__,strerror(ret));
		return;
	}
	worker_thread->thread_id = pthread_self();
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
	int tid = (last_tid + 1) % confitems.work_threads;
	libevent_thread *thread = threads + tid;
	last_tid = tid;

	item->sfd = sfd;
	item->init_state = conn_read;
	item->event_flags = EV_READ|EV_PERSIST;

	conn_item_queue_push(&thread->cq_head,item);

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

static enum network_read_result __network_read(conn *c)
{
	assert(c != NULL);
	enum network_read_result read_state = READ_NO_DATA_RECEIVED; 
	int allocs_count = 0;
	int res;
	
	while(1)
	{
		if(c->rbytes >= c->rsize)
		{
			if(allocs_count > 1)
			{
			   logger_error("file: "__FILE__", line: %d, " \
							"Read data buffer size of %d,likely "\
							" to be illegal packet.",\
						   	__LINE__,c->rsize);
				return READ_SOFT_ERROR;
			}
			++allocs_count;
			char *realloc_buff = realloc(c->rbuff,c->rsize * 2);
		   if(NULL == realloc_buff)
		   {
			   logger_error("file: "__FILE__", line: %d, " \
							"Out of memory reading request!", __LINE__);
			   //write error info to sock
			   c->writeto = conn_closed;
			   return READ_SOFT_ERROR;
		   }	   
		   c->rcurr = c->rbuff = realloc_buff;
		   c->rsize *= 2;
		}
		int avail = c->rsize - c->rbytes;
		res = read(c->sfd,c->rcurr + c->rbytes,avail);
		if(res > 0)
		{
			read_state = READ_DATA_RECEIVED;
			c->rbytes += res;
			if(res == avail)
				continue;
			else
				break;
		}		
		if(res == 0)
		{
			logger_info("file: "__FILE__", line: %d, " \
					"Socket fd:%d is closed,errno:%d," \
					"error info:%s!", __LINE__,c->sfd,errno,STRERROR(errno));
			return READ_SOFT_ERROR;
		}
		if(res == -1)
		{
			if(errno == EAGAIN || errno == EWOULDBLOCK)
				break;
			else
			{
				logger_error("file: "__FILE__", line: %d, " \
						"Failed to read socket,errno:%d," \
						"error info:%s!", __LINE__,errno,STRERROR(errno));
				return READ_HARD_ERROR;
			}
		}
	}
	return read_state;
}

static enum network_write_result __network_write_noblock(conn *c)
{
	assert(c != NULL);
	int written_bytes = 0;
	int remain_bytes = c->wbytes - c->wused;
	c->wcurr = c->wbuff + c->wused;
	
	if(remain_bytes > 0)
	{
		written_bytes = write(c->sfd,c->wcurr,remain_bytes);
		if(written_bytes > 0)
		{
			c->wused += written_bytes;
			return WRITE_INCOMPLETE;
		}
		if(written_bytes == -1 && (errno == EINTR || errno == EWOULDBLOCK))
		{
			if(update_conn_event(c,EV_WRITE | EV_PERSIST,__event_handler_callback) != 0)
			{
				logger_error("file: "__FILE__", line: %d, " \
							"Failed to update connection object event!", __LINE__);
				return WRITE_HARD_ERROR;
			}
			return WRITE_SOFT_ERROR;
		}
	}
	else
	{
		c->writeto = conn_waiting;
		return WRITE_COMPLETE;
	}
	return WRITE_INCOMPLETE;
}

static int __parse_protocol_cmd(conn *c)
{
	assert(c != NULL);
	assert(c->rbytes > 0);

	if(c->rbytes < sizeof(protocol_header))
		return LFS_OK;
	else
	{
		protocol_header *procl_header;
		procl_header = (protocol_header*)c->rbuff;
		if(procl_header->header_s.body_len > MAX_PACKAGE_DATA_SIZE)
		{
			handle_protocol_error(c,PROTOCOL_RESP_STATUS_ERROR_PACKAGESIZE_OVERFLOW);
			return LFS_ERROR;
		}
		if((c->rbytes - sizeof(protocol_header)) >= procl_header->header_s.body_len)
		{
			c->rbytes -= sizeof(protocol_header);
			c->rcurr = c->rbuff + sizeof(protocol_header);
			dispatch_cmd(c,procl_header->header_s.cmd);
		}
		else
		{
			return LFS_OK;
		}
	}
	return 1;
}

static void dispatch_cmd(conn *c,uint8_t cmd)
{
	protocol_resp_status resp_status;
	switch(cmd)
	{
		case PROTOCOL_CMD_DATASERVER_REGISTER:
			resp_status = handle_cmd_register(c);
			break;
		case PROTOCOL_CMD_DATASERVER_FULLSYNC:
			resp_status = handle_cmd_fullsyncreq(c);
			break;
		case PROTOCOL_CMD_DATASERVER_HEARTBEAT:
			resp_status = handle_cmd_heartbeat(c);
			break;
		case PROTOCOL_CMD_DATASERVER_SYNCREPORT:
			resp_status = handle_cmd_syncreport(c);
			break;
		case PROTOCOL_CMD_DATASERVER_STATREPORT:
			resp_status = handle_cmd_statreport(c);
			break;
		case PROTOCOL_CMD_DATASERVER_QUITED:
			resp_status = handle_cmd_quited(c);
			break;
		case PROTOCOL_CMD_WRITE_TRACKER_REQ:
			resp_status = handle_cmd_writetracker(c);
			break;
		case PROTOCOL_CMD_READ_TRACKER_REQ:
			resp_status = handle_cmd_readtracker(c);
			break;
		case PROTOCOL_CMD_TEST:
			resp_status = handle_cmd_test(c);
			break;
	}

	if(resp_status)
		handle_protocol_error(c,resp_status);
	return;
}
