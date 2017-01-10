/**
*
*
*
*
*
*
**/
#ifndef _DS_SYNC_H_
#define _DS_SYNC_H_

#include <pthread.h>
#include "dataserverd.h"
#include "ds_types.h"


#ifdef __cplusplus
extern "C"{
#endif

enum full_sync_state full_sync_from_master(connect_info *cinfo);
int async_thread_init(void);
int async_thread_destroy(void);
int async_thread_quit(void);
int async_thread_start(block_brief *bbrief);
int async_thread_exit(pthread_t *tid,block_brief *brief);

extern volatile int do_run_async_thread;
extern binlog_ctx bctx;
extern binlog_ctx rbctx;
#ifdef __cplusplus
}
#endif
#endif
