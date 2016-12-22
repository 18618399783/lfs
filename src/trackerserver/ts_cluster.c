/**
*
*
*
*
*
*
**/
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>

#include "logger.h"
#include "trackerd.h"
#include "ts_types.h"
#include "ts_datasevr.h"
#include "ts_wlc.h"
#include "ts_snapshot.h"
#include "ts_cluster.h"

#define DEFAULT_CLUSTER_HASH_POWER 2
#define DEFAULT_SLAVE_DATASEVER_ALLOC_SIZE 2

enum thread_state{
	THREAD_HANGUP = 0,
	THREAD_WAKEUP
};

static unsigned int cluster_lock_count;
static unsigned int cluster_lock_hashpower;
static pthread_mutex_t *cluster_locks;
static pthread_mutex_t cluster_maintenance_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cluster_maintenance_cond = PTHREAD_COND_INITIALIZER;
static pthread_t cluster_maintenance_tid = -1;
static volatile int do_run_cluster_maintenance_thread = 1;

datasevr_cluster clusters;

static int __cluster_init(const int init_hashsize);
static int __cluster_lock_init();
static void __cluster_free();
static void __cluster_start_expand(void); 
static void __cluster_expand();
static void* __cluster_maintenance_thread(void *arg);
static datasevr_volume* __do_find(const uint32_t vid);
static void __task_thread_state_set(enum thread_state tstate);


void cluster_lock(uint32_t hv)
{
	mutex_lock(&cluster_locks[hv & hashmask(cluster_lock_hashpower)]);
}

void* cluster_trylock(uint32_t hv)
{
	pthread_mutex_t *l = &cluster_locks[hv & hashmask(cluster_lock_hashpower)];
	if(pthread_mutex_trylock(l) == 0)
	{
		return l;
	}
	return NULL;
}

void cluster_unlock(uint32_t hv)
{
	mutex_unlock(&cluster_locks[hv & hashmask(cluster_lock_hashpower)]);
}

void cluster_tryunlock(void *lk)
{
	mutex_unlock((pthread_mutex_t*)lk);
}	

int cluster_init(const int init_hashsize)
{
	int ret = LFS_OK;
	if((ret = __cluster_init(init_hashsize)) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Failed init datasever primary_volumes context!", __LINE__);
		return ret;
	}
	if((ret = __cluster_lock_init()) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Failed to init cluster locks!", __LINE__);
		return ret;
	}
	if((ret = snapshot_load()) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Failed to load cluster snapshot!", __LINE__);
		return ret;
	}
	return ret;
}

int cluster_destroy()
{
	__cluster_free();
	pthread_mutex_destroy(&cluster_maintenance_lock);
	pthread_cond_destroy(&cluster_maintenance_cond);
	free(cluster_locks);
	wlcsl_free(clusters.wlcsl);
	return 0;
}

static void __cluster_free()
{
	if(clusters.primary_volumes == NULL) return;

	datasevr_volume **p;
	int i = 0;
	
	mutex_lock(&cluster_maintenance_lock);
	p = clusters.primary_volumes;
	for(i = 0;i < hashsize(clusters.hash_size);i++)
	{
		datasevr_volume *it,*next;
		pthread_mutex_t *lk = NULL;
		if((lk = cluster_trylock(i)))
		{
			for(it = clusters.primary_volumes[i]; NULL != it; it = next)
			{
				next = it->next;
				volume_free(it);
			}
			clusters.primary_volumes[i] = NULL;
		}
		else
		{
			usleep(1000);
		}
		if(lk)
		{
			cluster_tryunlock(lk);
			lk = NULL;
		}
	}
	free(p);
	p = NULL;
	mutex_unlock(&cluster_maintenance_lock);
	return;
}

int do_cluster_insert(datasevr_volume *dv)
{
	assert(dv != NULL);
	unsigned int oldbukt;

	if(clusters.expanding &&
			((oldbukt = (dv->volume_id & hashmask(clusters.hash_size))) >= clusters.expand_bucket))
	{
		dv->next = clusters.old_volumes[oldbukt];
		clusters.old_volumes[oldbukt] = dv;
	}
	else
	{
		dv->next = clusters.primary_volumes[dv->volume_id & hashmask(clusters.hash_size)];
		clusters.primary_volumes[dv->volume_id & hashmask(clusters.hash_size)] = dv;
	}
	clusters.primary_volume_count++;
	if((!clusters.expanding) && (clusters.primary_volume_count > (hashsize(clusters.hash_size * 1) / 2)))
	{
		__cluster_start_expand();
	}
	return LFS_OK;
}

datasevr_volume* do_cluster_find(const uint32_t vid)
{
	datasevr_volume *dvf;

	dvf = __do_find(vid);
	return dvf;
}

int cluster_maintenance_thread_start()
{
	int ret;
	pthread_mutex_init(&cluster_maintenance_lock,NULL);
	if((ret = pthread_create(&cluster_maintenance_tid,NULL,
					__cluster_maintenance_thread,NULL)) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Create cluster maintenance thread failed,errno:%d,\
				error info:%s!", __LINE__,ret,strerror(ret));
		return ret;
	}
	return LFS_OK;
}

void cluster_maintenance_thread_stop()
{
	mutex_lock(&cluster_maintenance_lock);
	do_run_cluster_maintenance_thread = 0;
	pthread_cond_signal(&cluster_maintenance_cond);
	mutex_unlock(&cluster_maintenance_lock);

	pthread_join(cluster_maintenance_tid,NULL);
}

static int __cluster_init(const int init_hashsize)
{
	if(init_hashsize)
	{
		clusters.hash_size = init_hashsize;
	}
	else
	{
		clusters.hash_size = DEFAULT_CLUSTER_HASH_POWER;
	}
	clusters.primary_volumes = (datasevr_volume**)malloc(sizeof(datasevr_volume*) * hashsize(clusters.hash_size));
	if(clusters.primary_volumes == NULL)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Failed to allocate memory to datasevr's primary_volumes!", __LINE__);
		return LFS_ERROR;
	}
	memset(clusters.primary_volumes,0,sizeof(datasevr_volume*) * hashsize(clusters.hash_size));
	clusters.old_volume_count = 0;
	clusters.primary_volume_count = 0;
	clusters.curr_volume_id = 0;
	clusters.expand_bucket = 0;
	clusters.is_started_expanding = false;
	clusters.expanding = false;
	clusters.old_volumes = NULL;
	clusters.wlcsl = wlcsl_create();
	return LFS_OK;
}

static int __cluster_lock_init()
{
	int lkc = clusters.hash_size - 1;
	lkc = (int)(hashsize(clusters.hash_size) / confitems.work_threads);
	if(lkc >= clusters.hash_size)
	{
		logger_warning("cluster lock table (%d) cannot be equal to or large cluster hash size (%d).",lkc,DEFAULT_CLUSTER_HASH_POWER);
	}
	cluster_lock_count = hashsize(lkc);
	cluster_lock_hashpower = lkc;
	cluster_locks = malloc(sizeof(pthread_mutex_t) * cluster_lock_count);
	if(cluster_locks == NULL)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Failed to allocate cluster lock!", __LINE__);
		return LFS_ERROR;
	}
	int i = 0;
	for(i = 0;i < cluster_lock_count; i++)
	{
		pthread_mutex_init(&cluster_locks[i],NULL);
	}
	return LFS_OK;
}

static void __cluster_start_expand(void)
{
	if(clusters.is_started_expanding)
		return;
	clusters.is_started_expanding = true;
	pthread_cond_signal(&cluster_maintenance_cond);
}

static void __cluster_expand()
{
	clusters.old_volumes = clusters.primary_volumes;
	clusters.old_volume_count = clusters.primary_volume_count;

	clusters.primary_volumes = malloc(sizeof(datasevr_volume*) * (hashsize(clusters.hash_size + 1)));
	if(clusters.primary_volumes)
	{
		memset(clusters.primary_volumes,0,(sizeof(datasevr_volume*) * (hashsize(clusters.hash_size + 1))));
		clusters.expand_bucket = 0;
		clusters.primary_volume_count = 0;
		clusters.hash_size++;
		clusters.expanding = true;
	}
	else
	{
		clusters.primary_volumes = clusters.old_volumes;
		clusters.primary_volume_count = clusters.old_volume_count;
		__task_thread_state_set(THREAD_WAKEUP);
	}
}
static void* __cluster_maintenance_thread(void *arg)
{
	mutex_lock(&cluster_maintenance_lock);	
	while(do_run_cluster_maintenance_thread)
	{
		while(clusters.expanding)
		{
			datasevr_volume *it,*next;
			int bucket;
			pthread_mutex_t *cluster_lock = NULL;
			if((cluster_lock = cluster_trylock(clusters.expand_bucket)))
			{
				for(it = clusters.old_volumes[clusters.expand_bucket];NULL != it;it = next)
				{
					next = it->next;
					bucket = it->volume_id & hashmask(clusters.hash_size);
					it->next = clusters.primary_volumes[bucket];
					clusters.primary_volumes[bucket] = it;
					clusters.primary_volume_count++;
				}
				clusters.old_volumes[clusters.expand_bucket] = NULL;
				clusters.expand_bucket++;
				if(clusters.expand_bucket == hashsize((clusters.hash_size - 1)))
				{
					clusters.expanding = false;
					free(clusters.old_volumes);
					clusters.old_volumes = NULL;
					clusters.old_volume_count = 0;
					__task_thread_state_set(THREAD_WAKEUP);
				}
			}
			else
			{
				usleep(10 * 1000);
			}
			if(cluster_lock)
			{
				cluster_tryunlock(cluster_lock);
				cluster_lock = NULL;
			}
		}
		if(!clusters.expanding)
		{
			clusters.is_started_expanding = false;
			pthread_cond_wait(&cluster_maintenance_cond,&cluster_maintenance_lock);
			__task_thread_state_set(THREAD_HANGUP);
			__cluster_expand();
			//__task_thread_state_set(THREAD_WAKEUP);
		}
	}
	return NULL;
}

static datasevr_volume* __do_find(const uint32_t vid)
{
	datasevr_volume* fdv = NULL;
	datasevr_volume* ite = NULL;
	unsigned int oldbkt;

	if(clusters.expanding && 
			((oldbkt = (vid & hashmask((clusters.hash_size - 1)))) >= clusters.expand_bucket))
	{
		ite = (void*)clusters.old_volumes[oldbkt];
	}
	else
	{
		ite = (void*)clusters.primary_volumes[vid & hashmask(clusters.hash_size)];
	}
	while(ite)
	{
		if(ite->volume_id == vid)
		{
			fdv = ite;
			break;
		}
		ite = ite->next;
	}
	return fdv;
}

static void __task_thread_state_set(enum thread_state tstate)
{
	switch(tstate)
	{
		case THREAD_HANGUP:
			snapshot_hangup();
			break;
		case THREAD_WAKEUP:
			snapshot_wakeup();
			break;
		default:
			logger_warning("Unknown thread state.");
			break;
	}
	return;
}
