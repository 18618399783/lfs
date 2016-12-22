/**
*
*
*
*
*
*
**/
#ifndef _DS_DISK_IO_H_
#define _DS_DISK_IO_H_

#include <pthread.h>

#include "ds_types.h"

#ifdef __cplusplus
extern "C"{
#endif

struct dio_thread_st{
	pthread_t tid;
	pthread_mutex_t lock;
	pthread_cond_t cond;
	queue_head dioq;
};
typedef struct dio_thread_st dio_thread;

struct dio_thread_pool_st{
	int count;
	int last_reader_tid;
	struct dio_thread_st *reader;
	struct dio_thread_st *write;
};
typedef struct dio_thread_pool_st dio_thread_pool;

int dio_thread_pool_init();
void dio_thread_pool_destroy();
int dio_thread_pool_dispatch(conn *c);
int dio_thread_pool_polling(conn *c);
int dio_read(conn *c);
int dio_write(conn *c);
void dio_write_error_cleanup(conn *c);
void dio_notify_nio(conn *c,enum conn_states state,short ev_flags);


#ifdef __cplusplus
}
#endif
#endif
