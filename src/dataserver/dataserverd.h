/**
*
*
*
*
*
*
**/
#ifndef _DATASERVERD_H_
#define _DATASERVERD_H_

#include <time.h>

#include "common_define.h"
#include "hash_table.h"
#include "lfs_protocol.h"
#include "lfs_types.h"
#include "ds_queue.h"
#ifdef __cplusplus
extern "C"{
#endif

#define LOCALHOST "127.0.0.1"
#define DEFAULT_PORT 2985
#define DEFAULT_MAXCONNS 1024
#define DEFAULT_NIO_THREADS_NUMBER 4
#define DEFAULT_DIO_READ_THREADS_NUMBER 1
#define DEFAULT_DIO_WRITE_THREADS_NUMBER 1
#define DEFAULT_BLOCK_COUNT 1
#define DEFAULT_NETWORK_TIMEOUT  60
#define DEFAULT_HEART_BEAT_INTERVAL  30
#define DEFAULT_SYNC_INTERVAL  40
#define DEFAULT_SYNC_WAIT_TIME_MSEC 100
#define DEFAULT_STATE_REPORT_INTERVAL 60
#define DEFAULT_BLOCK_VFS_INTERVAL 30
#define DEFAULT_BLOCK_SNAPSHOT_INTERVAL 30
#define BAT_DATA_SEPERATOR_SPLITSYMBOL '|'

#define CONN_ITEMS_PER_ALLOC 64
#define DEFAULT_NIO_READ_BUFF_SIZE 1024
#define DEFAULT_NIO_WRITE_BUFF_SIZE 1024
#define DEFAULT_FILE_BUFF_SIZE 64 * 1024
#define DEFAULT_THREAD_STACK_SIZE 512 * 1024
#define DEFAULT_BLOCK_SUB_COUNT 256
#define DEFAULT_BLOCK_MAX_FILE_COUNT 256
#define DEFAULT_BLOCK_FREE_SIZE_RATIO 0.15
#define DEFAULT_WEIGHT 1

#define DEFAULT_LISTEN_BACKLOG 1024


struct setting_st{
	int backlog;
	char *cfgfilepath;
};
typedef struct setting_st setting;

struct ctx_st{
	int max_fds;
	unsigned int curr_conns;
	unsigned int total_conns;
	unsigned int reserved_fds;
	uint64_t rejected_conns;
	time_t started_time;
	time_t register_timestamp;
	int64_t last_sync_sequence;
	bool is_fullsyncdone;
	int last_mount_block_index;
	int block_opt_count;
};


struct confs_st{
	char base_path[MAX_PATH_SIZE];
	char *group_name;
	char *volume_name;
	char *logger_level;
	char *logger_file_name;
	char *bind_addr;
	int bind_port;
	int network_timeout;
	int heart_beat_interval;
	int sync_report_interval;
	int sync_wait_time;
	int sync_interval;
	int state_report_interval;
	int block_vfs_interval;
	int block_snapshot_interval;
	int max_connections;
	int thread_stack_size;
	int nio_read_buffer_size;
	int nio_write_buffer_size;
	int file_buffer_size;
	int nio_threads;
	int dio_read_threads;
	int dio_write_threads;
	int block_count;
	char *block_mount;
	int block_sub_count;
	int block_max_file_count;
	float block_free_size_ratio;
	int weight;
	char *tracker_servers;
};
typedef struct confs_st confs;

extern char base_path[MAX_PATH_SIZE];
extern struct setting_st settings;
extern struct ctx_st ctxs;
extern struct confs_st confitems;
extern hash_table cfg_hashtable;
extern queue_head freecitems;

#ifdef __cplusplus
}
#endif
#endif
