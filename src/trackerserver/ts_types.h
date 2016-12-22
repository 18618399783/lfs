/**
*
*
*
*
*
**/
#ifndef _TS_TYPES_H_
#define _TS_TYPES_H_

#include "common_define.h"
#include "lfs_define.h"
#include "lfs_types.h"
#include "ts_wlc.h"

enum server_type{
	master = 0,
	slave
};

typedef struct datasevr_block_st{
	uint32_t parent_volume_id;
	uint32_t block_id;
	enum datasevr_state state;
	char volume_name[LFS_VOLUME_NAME_LEN_SIZE + 1];
	char ip_addr[IP_ADDRESS_SIZE];
	enum server_type type;
	int port;
	int curr_conns;
	int weight;
	int heart_beat_interval;
	int64_t total_mb;
	int64_t free_mb;
	time_t reg_time;
	time_t started_time;
	time_t last_synctimestamp;
	time_t last_heartbeat_time;
	time_t last_statreporttimestamp;
}datasevr_block;

typedef struct datasevr_volume_st{
	struct datasevr_volume_st *next;
	uint32_t volume_id;
	int block_count;
	int slave_block_count;
	int allocs_size;
	int write_block_index;
	int last_read_block_index;
	datasevr_block **blocks;
}datasevr_volume;

typedef struct datasevr_cluster_st{
	int old_volume_count;
	int primary_volume_count;
	uint32_t curr_volume_id;
	unsigned int hash_size;
	unsigned int expand_bucket;
	bool is_started_expanding;
	bool expanding;
	datasevr_volume **old_volumes;
	datasevr_volume **primary_volumes;
	wlc_skiplist *wlcsl;
}datasevr_cluster;

extern datasevr_cluster clusters;
#endif
