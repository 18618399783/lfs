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

#include "logger.h"
#include "ts_datasevr.h"
#include "ts_cluster.h"
#include "ts_cluster_datasevr.h"

#define CHECK_MASTER_FAILED_MAX_COUNT 3

static int __do_insert(datasevr_volume *dv,datasevr_block *dblk); 

int cluster_datasevrblock_insert(datasevr_block *dblk)
{
	assert(dblk != NULL);
	datasevr_volume *dv = NULL;
	uint32_t vid = dblk->parent_volume_id;
	cluster_lock(vid);
	dv = do_cluster_find(vid);
	__do_insert(dv,dblk);
	cluster_unlock(vid);
	return LFS_OK;
}

datasevr_volume* cluster_datasevrvolume_find(const char *vn,size_t nvn)
{
	datasevr_volume *dv = NULL;
	uint32_t vid;
	vid = hash_func((const void*)vn,nvn);
	cluster_lock(vid);
	dv = do_cluster_find(vid);
	cluster_unlock(vid);
	return dv;
}

datasevr_block* cluster_datasevrmasterblock_get(const char *vn,size_t nvn)
{
	datasevr_volume *dv = NULL;
	datasevr_block *dblk = NULL;
	uint32_t vid;
	vid = hash_func((const void*)vn,nvn);
	cluster_lock(vid);
	dv = do_cluster_find(vid);
	if((dv != NULL) && (dv->master_block_index >= 0))
	{
		dblk = dv->blocks[dv->master_block_index];
		logger_debug("master block index:%d,ip:%s.",dv->master_block_index,dblk->ip_addr);
	}
	cluster_unlock(vid);
	return dblk;
}

datasevr_block* cluster_wlc_writedatasevrblock_get()
{
	datasevr_block *dblk = NULL;

	dblk = cluster_wlc_write_block_get(clusters.wlcsl);
	return dblk;
}

datasevr_block* cluster_readdatasevrblock_get(const char *vn,size_t nvn,int64_t sequence)
{
	datasevr_volume *dv = NULL;
	datasevr_block *dblk = NULL;
	uint32_t vid;
	vid = hash_func((const void*)vn,nvn);
	cluster_lock(vid);
	dv = do_cluster_find(vid);
	if((dv != NULL) && (dv->blocks))
	{
		int i;
		for(i = 0; i < dv->block_count; i++)
		{
			time_t curr_time = time(NULL);
			if((i == dv->master_block_index) || \
					(i == dv->last_read_block_index))
				continue;
			if((dv->blocks[i]) && \
					((curr_time - dv->blocks[i]->last_heartbeat_time) <= \
					confitems.heart_beat_interval) && \
					(dv->blocks[i]->last_sync_sequence >= sequence))
			{
				dblk = dv->blocks[i];
				dv->last_read_block_index = i;
				break;
			}
		}
		if(dblk == NULL)
		{
			dblk = dv->blocks[dv->master_block_index];
		}
	}
	cluster_unlock(vid);
	return dblk;
}

datasevr_block* cluster_datasevrblock_find(const char *vn,size_t nvn,const char *bip,size_t nbip)
{
	datasevr_volume *dv = NULL;
	datasevr_block *dblk = NULL;
	uint32_t vid;
	uint32_t bid;
	vid = hash_func((const void*)vn,nvn);
	bid = hash_func((const void*)bip,nbip);
	cluster_lock(vid);
	dv = do_cluster_find(vid);
	dblk = do_block_find(dv,bid);
	cluster_unlock(vid);
	return dblk;
}

datasevr_block* cluster_datasevrblock_byid_find(uint32_t vid,uint32_t bid)
{
	datasevr_volume *dv = NULL;
	datasevr_block *dblk = NULL;
	cluster_lock(vid);
	dv = do_cluster_find(vid);
	dblk = do_block_find(dv,bid);
	cluster_unlock(vid);
	return dblk;
}

static int __do_insert(datasevr_volume *dv,datasevr_block *dblk)
{
	int ret;
	if(!dv)
	{
		dv = volume_new();
		if(!dv)
		{
			logger_error("file: "__FILE__", line: %d, " \
					"Failed allocating datasevr_volume memory to ip:%s !", __LINE__,dblk->ip_addr);
			return LFS_ERROR;
		}	
		dv->volume_id = dblk->parent_volume_id;
		do_cluster_insert(dv);
	}
	if((dv->block_count == 0) || \
			(dv->master_block_index == -1))
	{
		logger_debug("master block index:%d,ip:%s.",dv->master_block_index,dblk->ip_addr);
		dv->master_block_index = 0;
		dblk->type = master_server;
	}
	if((ret = do_block_insert(dv,dblk)) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"The (id %u,ip %s) datasevr block insert to the %s datasevr volume failed!", __LINE__,dblk->block_id,dblk->ip_addr,dblk->map_info);
		return ret;
	}
	return LFS_OK;
}


