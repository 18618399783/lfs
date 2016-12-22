/**
*
*
*
*
*
*
**/
#ifndef _DS_NETWORK_IO_H_
#define _DS_NETWORK_IO_H_

#include <event.h>
#include "dataserverd.h"
#include "ds_types.h"

#ifdef __cplusplus
extern "C"{
#endif
int nio_threads_init(int nthreads,struct event_base *main_base);
void nio_threads_destroy();
void nio_accept_loop(int sfd);
void nio_dispatch(int sfd);
extern libevent_dispatch_thread dispatch_thread;
extern volatile int do_run_nio_thread;
#ifdef __cplusplus
}
#endif

#endif

