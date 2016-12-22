/**
*
*
*
*
*
**/
#ifndef _TS_CLUSTER_H_
#define _TS_CLUSTER_H_

#include <time.h>
#include <pthread.h>

#include "trackerd.h"
#include "ts_types.h"
#include "lfs_protocol.h"

#ifdef __cplusplus
extern "C"{
#endif

#define DEFAULT_BUCKET_MOVE_SIZE 3
#define hashsize(n) ((unsigned long int)1<<(n))
#define hashmask(n) (hashsize(n)-1)

static inline int mutex_lock(pthread_mutex_t *mutex)
{
	while(pthread_mutex_trylock(mutex));
	return 0;
}
#define mutex_unlock(m) pthread_mutex_unlock(m)


void cluster_lock(uint32_t hv);
void* cluster_trylock(uint32_t hv);
void cluster_unlock(uint32_t hv);
void cluster_tryunlock(void *lk); 

int cluster_init();
int cluster_destroy();

int do_cluster_insert(datasevr_volume *dv);
datasevr_volume* do_cluster_find(const uint32_t vid);

int cluster_maintenance_thread_start();
void cluster_maintenance_thread_stop();


#ifdef __cplusplus
}
#endif
#endif
