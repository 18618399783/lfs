/**
*
*
*
*
*
*
**/
#ifndef _TS_THREAD_H_
#define _TS_THREAD_H_

#include <event.h>

#include "trackerd.h"

#ifdef __cplusplus
extern "C"{
#endif

int ts_thread_init(int nthreads,struct event_base *main_base);

void ts_thread_destroy();

void ts_accept_loop(int sfd);

void ts_dispatch(int sfd);

#ifdef __cplusplus
}
#endif

#endif

