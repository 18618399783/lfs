/**
*
*
*
*
*
*
**/
#ifndef _DS_TYPES_H_
#define _DS_TYPES_H_
#include <event.h>
#include <pthread.h>

#include "dataserverd.h"

#define MARK_FILENAME_SIZE 32
#define BINLOG_FILE_NAME_SIZE 256
#define BINLOG_RECORD_SIZE 256
#define BINLOG_BAT_LINE_BUFF_SIZE 256

#define FILE_OP_TYPE_CREATE_FILE 'C'
#define	FILE_OP_TYPE_APPEND_FILE 'A'
#define FILE_OP_TYPE_DELETE_FILE 'D'
#define FILE_OP_TYPE_UPDATE_FILE 'U'
#define FILE_OP_TYPE_MODIFY_FILE 'M'

#ifdef __cplusplus
extern "C"{
#endif

typedef struct file_ctx_st file_ctx;
typedef struct libevent_thread_st libevent_thread;
typedef struct libevent_dispatch_thread_st libevent_dispatch_thread;
typedef struct conn_item_st conn_item;
typedef struct conn_st conn;

typedef void (*FILE_OP_DONE_CALLBACK)(conn *c,const int err_no);
typedef int (*FILE_DIO_CALLBACK)(conn *c);
typedef void (*CLEANUP_CALLBACK)(conn *c);
typedef void (*event_callback)(const int fd,short which,void *arg);

enum network_read_result{
	READ_SUCCESS,
	READ_SOFT_ERROR,
	READ_HARD_ERROR,
	READ_DATA_RECEIVED,
	READ_NO_DATA_RECEIVED,
	READ_COMPLETE,
	READ_INCOMPLETE
};	

enum network_write_result{
	WRITE_SUCCESS,
	WRITE_SOFT_ERROR,
	WRITE_HARD_ERROR,
	WRITE_COMPLETE,
	WRITE_INCOMPLETE
};

enum conn_states{
	conn_parse_cmd,
	conn_read,
	conn_fread,
	conn_write,
	conn_fwrite,
	conn_fsend,
	conn_waiting,
	conn_mwrite,
	conn_n2dio,
	conn_closed
};

struct connect_info_st{
	int sfd;
	int port;
	char ipaddr[IP_ADDRESS_SIZE];
};
typedef struct connect_info_st connect_info;

struct conn_item_st{
	int sfd;
	enum conn_states init_state;
	int event_flags;
	struct list_head list;
};

struct conn_st{
	int sfd;
	enum conn_states state;
	enum conn_states nextto;
	struct libevent_thread_st *libevent_thread;
	struct event event;
	short event_flags;
	short which;
	char *rbuff;
	char *rcurr;
	int rsize;
	int alloc_count;
	int rbytes;
	char *wbuff;
	char *wcurr;
	int wsize;
	int wbytes;
	int wused;
	file_ctx *fctx;
	protocol_header header;
	struct list_head list;
};

struct libevent_thread_st{
	pthread_t thread_id;
	struct event_base *base;
	struct event notify_event;
	int notify_receive_fd;
	int notify_send_fd;
	queue_head ciq;
	queue_head cq;
};

struct libevent_dispatch_thread_st{
	pthread_t thread_id;
	struct event_base *base;
};

struct binlog_ctx_st{
	int binlog_fd;
	int curr_binlog_file_index;
	int64_t binlog_file_size;
	int binlog_wcache_buff_len;
	time_t binlog_file_update_timestamp;
	char binlog_path[MAX_PATH_SIZE];
	char binlog_mark_file_name[MAX_PATH_SIZE];
	char binlog_file_name[MAX_PATH_SIZE];
	char* binlog_wcache_buff;
};
typedef struct binlog_ctx_st binlog_ctx;

enum file_op_type{
	FILE_OP_TYPE_READ = 0,
	FILE_OP_TYPE_WRITE = 1,
};


struct file_ctx_st{
	int fd;
	int f_op_flags;
	int f_mnt_block_index;
	int f_mp_pre;
	int f_mp_suf;
	enum file_op_type f_op_type;
	char f_orginl_name[LFS_FILE_NAME_SIZE];
	char f_b64_name[LFS_BASE64_BUFF_SIZE];
	char f_map_name[LFS_FILE_MAP_NAME_SIZE];
	char f_block_map_name[LFS_FILE_BLOCK_MAP_NAME_SIZE];
	char f_id[LFS_FILE_ID_SIZE];
	char f_path_name[MAX_PATH_SIZE];
	time_t f_timestamp;
	int64_t f_offset;
	unsigned long f_crc32;
	int alloc_count;
	int64_t f_buff_size;
	int64_t f_buff_offset;
	int64_t f_roffset;
	int64_t f_woffset;
	int64_t f_size;
	int64_t f_total_size;
	int64_t f_total_offset;
	char *f_buff;
	FILE_OP_DONE_CALLBACK f_op_func;
	FILE_DIO_CALLBACK f_dio_func;
	CLEANUP_CALLBACK f_cleanup_func;
};	

struct block_brief_st{
	enum datasevr_state state;
	char ipaddr[IP_ADDRESS_SIZE];
	int port;
	int64_t last_sync_sequence;
};
typedef struct block_brief_st block_brief;

enum binlog_file_state{
	B_FILE_OK = 0,
	B_FILE_ERROR,
	B_FILE_NODATA,
	B_FILE_END
};

enum full_sync_state{
	F_SYNC_OK = 0,
	F_SYNC_ERROR,
	F_SYNC_MARK_ERROR,
	F_SYNC_NETWORK_ERROR,
	F_SYNC_NO_INIFILE,
	F_SYNC_FINISH
};

struct binlog_file_mete_st{
	int cindex;
	int64_t coffset;
	time_t cupdtimestamp;
};
typedef struct binlog_file_mete_st binlog_file_mete;

struct binlog_buff_st{
	char *buff;
	char *cbuff;
	int flag;
	int length;
};
typedef struct binlog_buff_st binlog_buff;

struct binlog_record_st{
	char op_type;
	char f_map_name[LFS_FILE_MAP_NAME_SIZE];
	char f_block_map_name[LFS_FILE_BLOCK_MAP_NAME_SIZE];
	char f_id[LFS_FILE_ID_SIZE];
	int f_id_length;
	int64_t sequence;
	int mnt_block_index;
};
typedef struct binlog_record_st binlog_record;

struct sync_file_req_st{
	char sync_file_name_len[LFS_STRUCT_PROP_LEN_SIZE4];
	char sync_file_name[MAX_PATH_SIZE];
};
typedef struct sync_file_req_st sync_file_req;

enum full_sync_binlog_type{
	LOCAL_BINLOG = 0,
	REMOTE_BINLOG
};

struct full_sync_binlog_mark_st{
	int b_file_count;
	int b_curr_sync_index;
	int rb_file_count;
	int rb_curr_sync_index;
	int64_t b_offset;
	time_t last_sync_timestamp;
};
typedef struct full_sync_binlog_mark_st full_sync_binlog_mark;

struct sync_ctx_st{
	char f_mark[MARK_FILENAME_SIZE];
	int b_index;
	int b_fd;
	int m_fd;
	time_t last_timestamp;
	binlog_buff b_buff;
	int64_t b_offset;
	int64_t sync_count; 
	int64_t last_sync_count;
};
typedef struct sync_ctx_st sync_ctx;

extern block_brief volume_blocks[LFS_MAX_BLOCKS_EACH_VOLUME];
extern int blocks_count;

#ifdef __cplusplus
}
#endif
#endif
