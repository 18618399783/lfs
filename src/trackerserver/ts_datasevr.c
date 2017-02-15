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

#include "lfs_define.h"
#include "logger.h"
#include "ts_types.h"
#include "ts_datasevr.h"



static int __datasevrvolume_insert(datasevr_volume *dv,datasevr_block *dblk);
static int __datasevrvolume_resize(datasevr_volume *dv);

datasevr_block* block_new()
{
	datasevr_block *dblk = NULL;
	
	dblk = (datasevr_block*)malloc(sizeof(datasevr_block));
	if(dblk == NULL)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Allocate datasevr_block memory failed!", __LINE__);
		return NULL;
	}
	memset(dblk,0,sizeof(datasevr_block));
	return dblk;
}

void block_free(datasevr_block *dblk)
{
	if(dblk)
	{
		free(dblk);
		dblk = NULL;
	}
	return;
}

datasevr_volume* volume_new()
{
	datasevr_volume *dv = NULL;

	dv = (datasevr_volume*)malloc(sizeof(datasevr_volume));
	if(dv == NULL)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Failed to allocate memory to dataserver group info!", __LINE__);
		return NULL;
	}
	memset(dv,0,sizeof(datasevr_volume));
	dv->blocks = (datasevr_block**)malloc(sizeof(datasevr_block*) * LFS_MAX_BLOCKS_EACH_VOLUME);
	if(dv->blocks == NULL)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Failed to allocate memory to datasevr volume blocks!", __LINE__);
		volume_free(dv);
		return NULL;
	}
	memset(dv->blocks,0,sizeof(datasevr_block*) * LFS_MAX_BLOCKS_EACH_VOLUME);
	dv->volume_id = 0;
	dv->block_count = 0;
	dv->allocs_size = LFS_MAX_BLOCKS_EACH_VOLUME;
	dv->master_block_index = -1;
	dv->last_read_block_index = 0;
	dv->check_master_failed_count = 0;
	return dv;
}

void volume_free(datasevr_volume *dv)
{
	if(dv == NULL) return;
	if(!dv->blocks)
	{
		datasevr_block **blks;
		datasevr_block **ites;
		datasevr_block **itee;
		blks = dv->blocks;
		ites = dv->blocks;
		itee = dv->blocks + dv->block_count;
		for(;ites < itee;ites++)
		{
			free(*ites);
			(*ites) = NULL;
		}
		free(blks);
		blks = NULL;
	}
	return;
}

int do_block_insert(datasevr_volume *dv,datasevr_block *dblk)
{
	int ret;
	assert(dblk != NULL);
	ret = __datasevrvolume_insert(dv,dblk);
	return ret;
}

datasevr_block* do_block_find(datasevr_volume *dv,uint32_t bid)
{
	if(dv == NULL) return NULL;
	if(dv->blocks)
	{
		datasevr_block **ites;
		datasevr_block **itee;
		ites = dv->blocks;
		itee = dv->blocks + dv->block_count;
		for(;ites < itee;ites++)
		{
			if((*ites) && ((*ites)->block_id == bid))
			{
				return *ites;
			}
		}
	}
	return NULL;
}

static int __datasevrvolume_insert(datasevr_volume *dv,datasevr_block *dblk)
{
	assert(dv != NULL);
	assert(dblk != NULL);
	int ret;

	if(dv->block_count >= dv->allocs_size)
	{
		if((ret = __datasevrvolume_resize(dv)) != 0)
		{
			logger_error("file: "__FILE__", line: %d, " \
					"The dataserver volume %s resize failed!", __LINE__,dblk->map_info);
			return ret;
		}
	}
	dv->blocks[dv->block_count] = dblk;
	dv->block_count++;
	logger_debug("the block id %u has inserted to volume %u,current block count is %d and index is %d.",\
			dblk->block_id,\
			dv->volume_id,\
			dv->block_count,\
			dv->master_block_index);
	return LFS_OK;
}

static int __datasevrvolume_resize(datasevr_volume *dv)
{
	assert(dv != NULL);
	assert(dv->blocks != NULL);

	datasevr_block **old_blocks;
	datasevr_block **new_blocks;
	datasevr_block **ites;
	datasevr_block **itee;
	int new_size;
	int i = 0;

	new_size = dv->allocs_size + LFS_MAX_BLOCKS_EACH_VOLUME;
	new_blocks = (datasevr_block**)malloc(sizeof(datasevr_block*) * new_size);
	if(new_blocks == NULL)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Allocate datasever volume id %d's blocks memery failed!", __LINE__,dv->volume_id);
		return LFS_ERROR;
	}
	memset(new_blocks,0,sizeof(datasevr_block*) * new_size);
	old_blocks = dv->blocks;
	ites = dv->blocks;
	itee = dv->blocks + dv->block_count;
	for(i = 0;ites < itee;i++,ites++)
	{
		new_blocks[i] = *ites;
	}
	free(old_blocks);
	old_blocks = NULL;
	dv->blocks = new_blocks;
	dv->allocs_size = new_size;
	return LFS_OK;
}
