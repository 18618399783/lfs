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
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <event.h>
#include <pthread.h>

#include "logger.h"
#include "shared_func.h"
#include "ds_types.h"
#include "ds_func.h"
#include "ds_queue.h"
#include "ds_conn.h"
#include "ds_file.h"
#include "ds_block.h"
#include "ds_disk_io.h"

volatile int do_run_dio_work_thread = true;
static int dio_init_threads_count = 0;
static pthread_mutex_t dio_tids_lock;
static pthread_cond_t dio_tids_cond;

static struct dio_thread_st *dio_tids = NULL;
static struct dio_thread_pool_st dio_pool;
static struct dio_thread_pool_st *pdio_pool = &dio_pool;

static int __setup_dio_thread(dio_thread *dio_tid);
static void* __dio_thread_entry(void *arg);
static void __create_dio_thread(void* (*func)(void*),void *arg);
static void __wait_for_dio_thread_startup(int nthreads);
static void __dio_thread_init_register(void);

static int __open_file(file_ctx *fctx,int64_t offset);

int dio_thread_pool_init()
{
	int i,dio_tids_count;

	dio_tids_count = confitems.dio_read_threads + \
					confitems.dio_write_threads;

	pthread_mutex_init(&dio_tids_lock,NULL);
	pthread_cond_init(&dio_tids_cond,NULL);

	dio_tids = calloc(dio_tids_count,sizeof(dio_thread));
	if(!dio_tids)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Allocate memory disk io threads failed,errno:%d",\
				"error info:%s!", __LINE__,errno,strerror(errno));
		return errno;
	}
	memset(dio_tids,0,(sizeof(dio_thread) * dio_tids_count));
	pdio_pool->count = dio_tids_count;
	pdio_pool->last_reader_tid = 0;
	pdio_pool->reader = dio_tids;
	pdio_pool->write = dio_tids + confitems.dio_read_threads;
	for(i = 0; i < dio_tids_count; i++)
	{
		__setup_dio_thread(&dio_tids[i]);
		__create_dio_thread(__dio_thread_entry,&dio_tids[i]);
	}
	pthread_mutex_lock(&dio_tids_lock);
	__wait_for_dio_thread_startup(dio_tids_count);
	pthread_mutex_unlock(&dio_tids_lock);
	return LFS_OK;
}

void dio_thread_pool_destroy()
{
	pthread_mutex_destroy(&dio_tids_lock);
	pthread_cond_destroy(&dio_tids_cond);
	do_run_dio_work_thread = 0;
	if(dio_tids != NULL)
	{
		free(dio_tids);
		dio_tids = NULL;
	}
}

int dio_thread_pool_dispatch(conn *c)
{
	assert(c != NULL);
	int ret = LFS_OK;

	if((ret = del_conn_event(c)) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"delete conn event failed.",\
			   	__LINE__);
		return ret;
	}
	ret = dio_thread_pool_polling(c); 
	return ret;
}

int dio_thread_pool_polling(conn *c)
{
	assert((c != NULL) &&(c->fctx != NULL));
	int ret;
	dio_thread *dio_t = NULL;

	if(c->fctx->f_op_type == FILE_OP_TYPE_READ)
	{
		pthread_mutex_lock(&dio_tids_lock);
		int tid = (pdio_pool->last_reader_tid + 1) \
				  % confitems.dio_read_threads;
		pdio_pool->last_reader_tid = tid;
		dio_t = &(pdio_pool->reader[tid]);
		pthread_mutex_unlock(&dio_tids_lock);
	}
	else if(c->fctx->f_op_type == FILE_OP_TYPE_WRITE)
	{
		int tid = (c->sfd + c->fctx->f_mnt_block_index) \
				  % confitems.dio_write_threads;
		dio_t = &(pdio_pool->write[tid]);
	}
	else
	{
		logger_warning("file: "__FILE__", line: %d, " \
				"There is no corresponding file operation type.",\
			   	__LINE__);
		return LFS_OK;
	}
	assert(dio_t != NULL);
	conn_queue_push(&dio_t->dioq,c);
	if((ret = pthread_cond_signal(&dio_t->cond)) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"dio pthread signal failed,errno:%d,"\
				"error info:%s!", __LINE__,ret,strerror(ret));
		return ret;
	}
	return LFS_OK;
}

int dio_read(conn *c)
{
	assert((c != NULL) && (c->fctx != NULL));
	int ret = 0;
	int64_t remain_bytes;
	int buff_bytes;
	int read_bytes;
	file_ctx *fctx;
	fctx = c->fctx;
	
	do
	{
		if(c->fctx->f_offset >= c->fctx->f_size)
		{
			break;
		}
		if((ret = __open_file(fctx,fctx->f_offset)) != 0)
		{
			break;
		}
		remain_bytes = fctx->f_size - fctx->f_offset;
		buff_bytes = fctx->f_buff_size - fctx->f_buff_offset;
		read_bytes = (buff_bytes < remain_bytes)?buff_bytes:remain_bytes;
		if(read(fctx->fd,fctx->f_buff + fctx->f_buff_offset,\
					read_bytes) != read_bytes)
		{
			logger_error("file: "__FILE__", line: %d, " \
					"read the original file name %s,block map name %s failed,errno:%d,"\
					"error info:%s!",\
					__LINE__,fctx->f_orginl_name,fctx->f_block_map_name,errno,strerror(errno));
			ret = LFS_ERROR;
			break;
		}
		fctx->f_buff_offset += read_bytes;
		dio_notify_nio(c,conn_fwrite,EV_WRITE|EV_PERSIST);
		ret = LFS_OK;
	}while(0);
	if(fctx->fd > 0)
	{
		close(fctx->fd);
		fctx->fd = -1;
	}
	if(fctx->f_op_func != NULL)
	{
		fctx->f_op_func(c,ret);
	}
	return ret;
}

int dio_write(conn *c)
{
	assert((c != NULL) && (c->fctx != NULL));
	int ret = LFS_OK;
	file_ctx *fctx = NULL;
	fctx = c->fctx;
	int write_bytes,write_fin_bytes;

	do{
		if(fctx->fd < 0)
		{
			if((ret = __open_file(fctx,fctx->f_offset)) != 0)
			{
				break;
			}
		}
		write_bytes = fctx->f_buff_offset;
		if((write_fin_bytes = write(fctx->fd,fctx->f_buff,write_bytes)) != write_bytes)
		{
			logger_error("file: "__FILE__", line: %d, " \
					"Write block map name %s file failed,errno:%d,"\
					"error info:%s!",\
					__LINE__,fctx->f_block_map_name,errno,strerror(errno));
			ret = LFS_ERROR;
			break;
		}
		fctx->f_offset += write_fin_bytes;
		fctx->f_total_offset += write_fin_bytes;
		if(fctx->f_offset < fctx->f_size)
		{
			file_ctx_buff_reset(fctx);
			dio_notify_nio(c,conn_fread,EV_READ|EV_PERSIST);
			return LFS_OK;
		}
		if(fctx->fd > 0)
		{
			close(fctx->fd);
			fctx->fd = -1;
		}
		if(fctx->f_op_func != NULL)
		{
			fctx->f_op_func(c,ret);
		}
		return LFS_OK;
	}while(0);
	if(fctx->f_cleanup_func != NULL)
	{
		fctx->f_cleanup_func(c);
	}
	if(fctx->f_op_func != NULL)
	{
		fctx->f_op_func(c,ret);
	}
	return ret;
}

void dio_write_error_cleanup(conn *c)
{
	assert((c != NULL) && (c->fctx != NULL));
	file_ctx *fctx = c->fctx;
	if(fctx->fd > 0)
	{
		close(fctx->fd);
		fctx->fd = -1;
		if(fctx->f_rwoffset < fctx->f_size)
		{
			file_unlink(fctx);
			return;
		}
	}
}

void dio_notify_nio(conn *c,enum conn_states state,short ev_flags)
{
	assert(c != NULL);
	char buf[1];
	c->state = state;
	c->event_flags = ev_flags;
	libevent_thread *thread = c->libevent_thread;
	conn_queue_push(&thread->cq,c);
	buf[0] = 'd';
	if(write(thread->notify_send_fd,buf,1) != 1)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"dio thread notify nio thread failed,errno:%d,"\
				"error info:%s!", __LINE__,errno,strerror(errno));
	}
	return;
}

static int __setup_dio_thread(dio_thread *dio_tid)
{
	int ret;
	if((ret = pthread_mutex_init(&dio_tid->lock,NULL)) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"Failed init pthread lock: %s",\
				   	__LINE__,strerror(ret));
		return ret;
	}
	if((ret = pthread_cond_init(&dio_tid->cond,NULL)) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"Failed init pthread cond: %s",\
				   	__LINE__,strerror(ret));
		return ret;
	}
	init_queue(&(dio_tid->dioq));
	return LFS_OK;
}

static void* __dio_thread_entry(void *arg)
{
	conn *c = NULL;
	dio_thread *dio_t = (dio_thread*)arg;
	__dio_thread_init_register();
	pthread_mutex_lock(&(dio_t->lock));
	while(do_run_dio_work_thread)
	{
		pthread_cond_wait(&dio_t->cond,&dio_t->lock);
		while((c = conn_queue_pop(&dio_t->dioq)) != NULL)
		{
			c->fctx->f_dio_func(c);
		}
	}
	pthread_mutex_unlock(&(dio_t->lock));
	return NULL;
}

static void __create_dio_thread(void* (*func)(void*),void *arg)
{
	pthread_t tid;
	pthread_attr_t attr;
	int ret;

	dio_thread *dio_t = (dio_thread*)arg;
	if((ret = init_pthread_attr(&attr,confitems.thread_stack_size)) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"Failed init pthread attr: %s", __LINE__,strerror(ret));
		return;
	}
	if((ret = pthread_create(&tid,&attr,func,arg)) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"Can't create dio thread: %s", __LINE__,strerror(ret));
		return;
	}
	dio_t->tid = pthread_self();
	pthread_attr_destroy(&attr);
	return;
}

static void __wait_for_dio_thread_startup(int nthreads)
{
	while(dio_init_threads_count < nthreads)
	{
		pthread_cond_wait(&dio_tids_cond,&dio_tids_lock);
	}
	return;
}

static void __dio_thread_init_register(void)
{
	pthread_mutex_lock(&dio_tids_lock);
	dio_init_threads_count++;
	pthread_cond_signal(&dio_tids_cond);
	pthread_mutex_unlock(&dio_tids_lock);
	return;
}

static int __open_file(file_ctx *fctx,int64_t offset)
{
	if(fctx->fd >= 0)
	{
		return 0;
	}
	fctx->fd = open(fctx->f_block_map_name,fctx->f_op_flags,0644);
	if(fctx->fd < 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"open block map name %s failed,errno:%d,"\
				"error info:%s!",\
			   	__LINE__,fctx->f_block_map_name,errno,strerror(errno));
		return errno;
	}
	if(offset > 0 && lseek(fctx->fd,offset,SEEK_SET) < 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"lseek the block map name %s file failed,errno:%d,"\
				"error info:%s!",\
			   	__LINE__,fctx->f_block_map_name,errno,strerror(errno));
		return errno;
	}
	return LFS_OK;
}

