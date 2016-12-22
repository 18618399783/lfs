/**
*
*
*
*
*
**/
#ifndef _TS_SNAPSHOT_H_
#define _TS_SNAPSHOT_H_

#include "trackerd.h"

#ifdef __cplusplus
extern "C"{
#endif

enum snapshot_stat{
	SNAPSHOT_STARTED = 0,
	SNAPSHOT_STOP,
	SNAPSHOT_RUNNING
};

int snapshot_init(void);
int snapshot_destroy(void);
int snapshot_thread_start(void);
int snapshot_thread_stop(void);
void snapshot_hangup(void);
void snapshot_wakeup(void);
enum snapshot_stat do_snapshot(void);
int snapshot_load();


#ifdef __cplusplus
}
#endif
#endif
