/**
*
*
*
*
*
*
*
**/
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <assert.h>


#include "logger.h"
#include "shared_func.h"
#include "ts_types.h"
#include "ts_func.h"
#include "ts_cluster.h"
#include "ts_datasevr.h"
#include "ts_cluster_datasevr.h"
#include "ts_snapshot.h"

#define VOLUME_BLOCK_SNAPSHOT_FILENAME "volume_blocks.dat"
#define VOLUME_BLOCK_BINLOG_DATAFILEDS 11

static pthread_t snapshot_tid = -1;
static int snapshot_thread_isinit = 0;
static volatile int do_run_snapshot_thread = 0;
static pthread_mutex_t snapshot_lock;
static pthread_cond_t snapshot_cond;


static void* __snapshot_thread(void *arg);
static void __snapshot_flush();
static int __snapshot_data_load(const char *fpath);


int snapshot_init(void)
{
	int ret;
	char fpath[MAX_PATH_SIZE];
	snprintf(fpath,sizeof(fpath),"%s/bin",base_path);
	if(!fileExists(fpath))
	{
		if(mkdir(fpath,0755) != 0)
		{
			logger_error("file: "__FILE__", line: %d," \
					"Mkdir \"%s\",errno:%d," \
					"error info:%s!", __LINE__,fpath,errno,strerror(errno));
			return errno;
		}
	}
	if(snapshot_thread_isinit == 0)
	{
		pthread_mutex_init(&snapshot_lock,NULL);
		if((ret = pthread_cond_init(&snapshot_cond,NULL)) != 0)
		{
			logger_error("file: "__FILE__", line: %d," \
					"Can't init snapshot condition,errno:%d," \
					"error info:%s!", __LINE__,ret,strerror(ret));
			return ret;
		}
		snapshot_thread_isinit = 1;
	}
	return LFS_OK;
}

int snapshot_thread_start(void)
{
	int ret;
	pthread_mutex_lock(&snapshot_lock);
	do_run_snapshot_thread = 1;
	if((ret = pthread_create(&snapshot_tid,NULL,
					__snapshot_thread,NULL)) != 0)
	{
		logger_error("file: "__FILE__", line: %d," \
				"Create snapshot thread failed,errno:%d," \
				"error info:%s!", __LINE__,ret,strerror(ret));
		pthread_mutex_unlock(&snapshot_lock);
		return ret;
	}
	pthread_mutex_unlock(&snapshot_lock);
	return LFS_OK;
}

int snapshot_destroy(void)
{
	pthread_mutex_destroy(&snapshot_lock);
	pthread_cond_destroy(&snapshot_cond);
	return LFS_OK;
}

int snapshot_thread_stop(void)
{
	assert(snapshot_tid != -1);
	int ret;
	pthread_mutex_lock(&snapshot_lock);
	do_run_snapshot_thread = 0;
	pthread_cond_signal(&snapshot_cond);
	pthread_mutex_unlock(&snapshot_lock);

	if((ret = pthread_join(snapshot_tid,NULL)) != 0)
	{
		logger_error("file: "__FILE__", line: %d," \
				"Failed to stop snapshot thread,errno:%d," \
				"error info:%s!", __LINE__,ret,strerror(ret));
		return ret;
	}
	return LFS_OK;
}

void snapshot_hangup(void)
{
	pthread_mutex_lock(&snapshot_lock);
}

void snapshot_wakeup(void)
{
	pthread_mutex_unlock(&snapshot_lock);
}

enum snapshot_stat do_snapshot(void)
{
	if(pthread_mutex_trylock(&snapshot_lock) != 0)
	{
		return SNAPSHOT_RUNNING;
	}
	pthread_cond_signal(&snapshot_cond);
	pthread_mutex_unlock(&snapshot_lock);
	return SNAPSHOT_STARTED;
}

int snapshot_load()
{
	int ret;
	char fpath[MAX_PATH_SIZE];
	snprintf(fpath,sizeof(fpath),"%s/bin",base_path);

	if((ret = chdir(fpath)) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Chdir \"%s\",errno:%d," \
				"error info:%s!", __LINE__,fpath,ret,strerror(ret));
		return ret;
	}
	if(!fileExists(VOLUME_BLOCK_SNAPSHOT_FILENAME))
		return LFS_OK;
	if((ret = __snapshot_data_load(fpath)) != 0)
	{
		return ret;
	}
	return LFS_OK;
}

static void* __snapshot_thread(void *arg)
{
	pthread_mutex_lock(&snapshot_lock);
	while(do_run_snapshot_thread)
	{
		pthread_cond_wait(&snapshot_cond,&snapshot_lock);
		__snapshot_flush();
	}
	pthread_mutex_unlock(&snapshot_lock);
	return NULL;
}

static void __snapshot_flush()
{
	int ret = LFS_OK;
	int i;
	int fd;
	int len;
	char fname[MAX_PATH_SIZE];
	char tfname[MAX_PATH_SIZE];
	char buff[LFS_VOLUME_NAME_LEN_SIZE + IP_ADDRESS_SIZE + 64];

	MEM_FILE_LOCK();
	snprintf(fname,sizeof(fname),"%s/bin/%s",\
			base_path,VOLUME_BLOCK_SNAPSHOT_FILENAME);
	snprintf(tfname,sizeof(tfname),"%s.tmp",fname);
	if((fd = open(tfname,O_WRONLY | O_CREAT | O_TRUNC,0644)) < 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Open \"%s\" failed,errno:%d," \
				"error info:%s!", __LINE__,tfname,errno,strerror(errno));
		MEM_FILE_UNLOCK();
		return;
	}
	for(i = 0;i < hashsize(clusters.hash_size); i++)
	{
		void *hlck = NULL;
		datasevr_volume *it,*next;
		if((hlck = cluster_trylock(i)))
		{
			for(it = clusters.primary_volumes[i]; NULL != it; it = next)
			{
				next = it->next;
				datasevr_block **blks;
				blks = it->blocks;
				int j;
				if(blks)
				{
					for(j = 0; j < it->block_count; j++)
					{
						if(blks[j])
						{
							len = sprintf(buff,\
									"%u%c%u%c%s%c%s%c%d%c%d%c%d%c%d%c%ld%c%ld%c%ld\n",\
									blks[j]->parent_volume_id,\
									BAT_DATA_SEPERATOR_SPLITSYMBOL,\
									blks[j]->block_id,\
									BAT_DATA_SEPERATOR_SPLITSYMBOL,\
									blks[j]->map_info,\
									BAT_DATA_SEPERATOR_SPLITSYMBOL,\
									blks[j]->ip_addr,\
									BAT_DATA_SEPERATOR_SPLITSYMBOL,\
									blks[j]->type,\
									BAT_DATA_SEPERATOR_SPLITSYMBOL,\
									blks[j]->port,\
									BAT_DATA_SEPERATOR_SPLITSYMBOL,\
									blks[j]->weight,\
									BAT_DATA_SEPERATOR_SPLITSYMBOL,\
									blks[j]->heart_beat_interval,\
									BAT_DATA_SEPERATOR_SPLITSYMBOL,\
									(long int)blks[j]->reg_time,\
									BAT_DATA_SEPERATOR_SPLITSYMBOL,\
									(long int)blks[j]->started_time,\
									BAT_DATA_SEPERATOR_SPLITSYMBOL,\
									(int64_t)blks[j]->last_sync_sequence);
							if(write(fd,buff,len) != len)
							{
								logger_error("file: "__FILE__", line: %d, " \
										"Write volume name %s to \"%s\" failed,errno:%d," \
										"error info:%s!", __LINE__,blks[j]->map_info,VOLUME_BLOCK_SNAPSHOT_FILENAME,errno,strerror(errno));
								ret = (errno != 0)?errno:EIO;
								break;
							}
						}
					}	
				}
			}
		}
		else
		{
			usleep(10 * 1000);
		}
		if(hlck)
		{
			cluster_tryunlock(hlck);
			hlck = NULL;
		}
	}
	if(ret == 0)
	{
		if(fsync(fd) != 0)
		{
			logger_error("file: "__FILE__", line: %d," \
					"Fsync file \"%s\" failed,errno:%d," \
					"error info:%s!",\
					__LINE__,tfname,errno,strerror(errno));
			ret = (errno != 0)?errno:EIO;
		}
	}
	close(fd);
	if(ret == 0)
	{
		if(rename(tfname,fname) != 0)
		{
			logger_error("file: "__FILE__", line: %d," \
					"Rename file \"%s\" to \"%s\" failed,errno:%d," \
					"error info:%s!",\
				   	__LINE__,tfname,fname,errno,strerror(errno));
			ret = (errno != 0)?errno:EIO;
		}
	}
	MEM_FILE_UNLOCK();
	return;
}

static int __snapshot_data_load(const char *fpath)
{
	int ret = 0;
	FILE *fp;
	char sline[256];
	char *fields[VOLUME_BLOCK_BINLOG_DATAFILEDS];
	int col_count;
	datasevr_block *blk = NULL;

	if((fp = fopen(VOLUME_BLOCK_SNAPSHOT_FILENAME,"r")) == NULL)	
	{
		logger_error("file: "__FILE__", line: %d," \
				"Open file \"%s\",errno:%d," \
				"error info:%s!", __LINE__,VOLUME_BLOCK_SNAPSHOT_FILENAME,errno,strerror(errno));
		return ret;
	}
	while(fgets(sline,sizeof(sline),fp) != NULL)
	{
		if(*sline == '\0')
		{
			continue;
		}
		col_count = splitStr(sline,BAT_DATA_SEPERATOR_SPLITSYMBOL,\
				fields,VOLUME_BLOCK_BINLOG_DATAFILEDS);
		if(col_count != VOLUME_BLOCK_BINLOG_DATAFILEDS && \
				col_count != VOLUME_BLOCK_BINLOG_DATAFILEDS - 8)
		{
			logger_error("file: "__FILE__", line: %d," \
					"The snapshot of the file  \"%s/%s\" is invalid!",\
				   	__LINE__,fpath,VOLUME_BLOCK_SNAPSHOT_FILENAME);
			break;
		}
		blk = block_new();
		if(blk == NULL)
		{
			logger_error("file: "__FILE__", line: %d," \
					"In the snapshot of the file  \"%s/%s\",ip %s's block allocate memory is failed!",\
				   	__LINE__,fpath,VOLUME_BLOCK_SNAPSHOT_FILENAME,fields[3]);
			break;
		}
		blk->parent_volume_id = atoi(trim(fields[0]));
		blk->block_id = atoi(trim(fields[1]));
		snprintf(blk->map_info,sizeof(blk->map_info),"%s",trim(fields[2]));
		snprintf(blk->ip_addr,sizeof(blk->ip_addr),"%s",trim(fields[3]));
		blk->type = atoi(trim(fields[4]));
		blk->port = atoi(trim(fields[5]));
		blk->weight = atoi(trim(fields[6]));
		blk->heart_beat_interval = atoi(trim(fields[7]));
		blk->reg_time = (time_t)atol(trim(fields[8]));
		blk->started_time = (time_t)atol(trim(fields[9]));
		blk->last_sync_sequence = atol(trim(fields[10]));
		blk->state = off_line;
		if((ret = cluster_datasevrblock_insert(blk)) != 0)	
		{
			logger_error("file: "__FILE__", line: %d," \
					"In the snapshot of the file  \"%s/%s\",ip %s's block insert failed!",\
				   	__LINE__,fpath,VOLUME_BLOCK_SNAPSHOT_FILENAME,fields[3]);
			break;
		}
	}
	fclose(fp);
	return LFS_OK;
}
