/**
*
*
*
*
*
*
**/
#ifndef _TRACKERD_H_
#define _TRACKERD_H_

#include <event.h>
#include <pthread.h>
#include <time.h>

#include "common_define.h"
#include "list.h"
#include "hash_table.h"
#include "lfs_protocol.h"

#define LOCALHOST "127.0.0.1"
#define DEFAULT_PORT 2985
#define DEFAULT_MAXCONNS 1024
#define DEFAULT_LISTEN_BACKLOG 1024
#define DEFAULT_THREADS_NUMBER 4
#define DEFAULT_NETWORK_TIMEOUT  60
#define DEFAULT_HEART_BEAT_INTERVAL  30

#define CONN_ITEMS_PER_ALLOC 64
#define DEFAULT_CONN_BUFF_SIZE 4 * 1024
#define DEFAULT_THREAD_STACK_SIZE 512 * 1024
#define DEFAULT_SNAPSHOT_THREAD_INTERVAL 30
#define BAT_DATA_SEPERATOR_SPLITSYMBOL '|'

#ifdef __cplusplus
extern "C"{
#endif
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
};

struct confs_st{
	char base_path[MAX_PATH_SIZE];
	char *logger_level;
	char *logger_file_name;
	char *bind_addr;
	int bind_port;
	int network_timeout;
	int max_connections;
	int thread_stack_size;
	int conn_buffer_size;
	int work_threads;
	int heart_beat_interval;
	int snapshot_thread_interval;
};
typedef struct confs_st confs;

enum conn_states{
	conn_parse_cmd,
	conn_read,
	conn_nread,
	conn_write,
	conn_waiting,
	conn_closed
};

struct queue_head_st{
	pthread_mutex_t lock;	
	struct list_head head;
};
typedef struct queue_head_st queue_head; 

struct conn_item_st{
	int sfd;
	enum conn_states init_state;
	int event_flags;
	struct list_head list;
};
typedef struct conn_item_st conn_item;

struct libevent_thread_st{
	pthread_t thread_id;
	struct event_base *base;
	struct event notify_event;
	int notify_receive_fd;
	int notify_send_fd;
	queue_head cq_head;
};
typedef struct libevent_thread_st libevent_thread;

struct libevent_dispatch_thread_st{
	pthread_t thread_id;
	struct event_base *base;
};
typedef struct libevent_dispatch_thread_st libevent_dispatch_thread;

struct conn_st{
	int sfd;
	enum conn_states state;
	enum conn_states writeto;
	struct libevent_thread_st *libevent_thread;
	struct event event;
	short event_flags;
	short which;
	char *rbuff;
	char *rcurr;
	int rsize;
	int rbytes;
	int rlbytes;
	char *wbuff;
	char *wcurr;
	int wsize;
	int wbytes;
	int wused;
};
typedef struct conn_st conn;

extern char base_path[MAX_PATH_SIZE];
extern struct setting_st settings;
extern struct ctx_st ctxs;
extern struct confs_st confitems;
extern hash_table cfg_hashtable;
extern queue_head freecitems;
extern conn **conns;
extern libevent_dispatch_thread dispatch_thread;
extern volatile bool main_thread_flag;
#ifdef __cplusplus
}
#endif
#endif
