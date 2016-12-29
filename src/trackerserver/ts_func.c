/**
*
*
*
*
*
*
**/
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>

#include "logger.h"
#include "hash.h"
#include "hash_table.h"
#include "cfg_file_opt.h"
#include "times.h"
#include "sched_timer.h"
#include "shared_func.h"
#include "sock_opt.h"
#include "ts_queue.h"
#include "ts_thread.h"
#include "ts_cluster.h"
#include "ts_wlc.h"
#include "ts_snapshot.h"

#include "ts_func.h"

static pthread_mutex_t ctxs_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mem_file_lock = PTHREAD_MUTEX_INITIALIZER;

void settings_init(void)
{
	settings.backlog = DEFAULT_LISTEN_BACKLOG;
	settings.cfgfilepath = NULL;
}

void ctxs_init(void)
{
	ctxs.max_fds = 0;
	ctxs.curr_conns = 0;
	ctxs.total_conns = 0;
	ctxs.rejected_conns = 0;
	ctxs.reserved_fds = 5;
	ctxs.started_time = time(NULL);
}

void conf_items_init(void)
{
	confitems.logger_level = "INFO";
	confitems.logger_file_name = "trackerd";
	confitems.bind_addr = LOCALHOST;
	confitems.bind_port = DEFAULT_PORT;
	confitems.network_timeout = DEFAULT_NETWORK_TIMEOUT;
	confitems.max_connections = DEFAULT_MAXCONNS;
	confitems.thread_stack_size = DEFAULT_THREAD_STACK_SIZE;
	confitems.conn_buffer_size = DEFAULT_CONN_BUFF_SIZE;
	confitems.work_threads = DEFAULT_THREADS_NUMBER;
	confitems.heart_beat_interval = DEFAULT_HEART_BEAT_INTERVAL;
	confitems.snapshot_thread_interval = DEFAULT_SNAPSHOT_THREAD_INTERVAL; 
}

void conns_init(void)
{
	int next_fd = dup(1);
	struct rlimit rl;

	ctxs.max_fds = confitems.max_connections + ctxs.reserved_fds + next_fd;
	if(getrlimit(RLIMIT_NOFILE,&rl) == 0)
	{
		ctxs.max_fds = rl.rlim_max;
	}
	else
	{
		fprintf(stderr,"Failed to query maximum file descriptor.");
	}
	close(next_fd);

	init_queue(&freecitems);
	
	if((conns = calloc(ctxs.max_fds,sizeof(conn*))) == NULL)
	{
		fprintf(stderr,"Failed to allocate connection pool!\n");
		exit(1);
	}
}

int server_sockets(const char *bind_host,const int port,const int timeout)
{
	int sfd;
	int flags = 1;

	sfd = socket(AF_INET,SOCK_STREAM,0);
	if(sfd < 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"Failed to create socket,errno:%d,error info:\
					 %s!", __LINE__,errno,STRERROR(errno));
		return -1;
	}
	if(setsockopt(sfd,SOL_SOCKET,SO_REUSEADDR,(void*)&flags,(socklen_t)sizeof(int)) != 0)
		logger_warning("file: "__FILE__", line: %d, " \
					"Failed to setsockopt,errno:%d,error info:\
					 %s!", __LINE__,errno,STRERROR(errno));
	if(set_sock_opt(sfd,timeout) != 0)
	{
		logger_warning("file: "__FILE__", line: %d, " \
					"Failed to setsockopt,errno:%d,error info:\
					 %s!", __LINE__,errno,STRERROR(errno));
	}

	if(socket_bind(sfd,bind_host,port) != 0)
	{
		close(sfd);
		return -2;
	}
	if(listen(sfd,settings.backlog) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"Failed to listen port: %d,errno:%d,error info:\
					 %s!", __LINE__,port,errno,STRERROR(errno));
		close(sfd);
		return -3;
	}
	if(set_noblock(sfd) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"Failed to setting noblock: %d,errno:%d,error info:\
					 %s!", __LINE__,port,errno,STRERROR(errno));
		close(sfd);
		return -4;
	}
	return sfd;
}

void tracker_destroy()
{
	cfg_destroy(&cfg_hashtable);
	logger_destroy();	
	cluster_maintenance_thread_stop();
	snapshot_thread_stop();
	sched_timer_exit();
	cluster_destroy();
	snapshot_destroy();
	main_thread_flag = false;
	ts_thread_destroy();
}

int set_cfg2globalobj()
{
	char *pbp = NULL;
	pbp = cfg_get_strvalue(&cfg_hashtable,"base.path");
	if(pbp == NULL)
	{
		fprintf(stderr,"The cfg file must include the \"base.path\"items!\n");
		return LFS_ERROR;
	}
	memset(base_path,0,MAX_PATH_SIZE);
	snprintf(base_path,sizeof(base_path),"%s",pbp);
	confitems.logger_level = cfg_get_strvalue(&cfg_hashtable,"logger.level");
	confitems.logger_file_name = cfg_get_strvalue(&cfg_hashtable,"logger.file.name");
	confitems.bind_addr = cfg_get_strvalue(&cfg_hashtable,"bind.addr");
	confitems.bind_port = cfg_get_intvalue(&cfg_hashtable,"bind.port",DEFAULT_PORT);
	confitems.network_timeout = cfg_get_intvalue(&cfg_hashtable,"network.timeout",DEFAULT_NETWORK_TIMEOUT);
	confitems.max_connections = cfg_get_intvalue(&cfg_hashtable,"max.connections",DEFAULT_MAXCONNS);
	confitems.thread_stack_size = cfg_get_intvalue(&cfg_hashtable,"thread.stack.size",DEFAULT_THREAD_STACK_SIZE);
	confitems.conn_buffer_size = cfg_get_intvalue(&cfg_hashtable,"conn.buffer.size",DEFAULT_CONN_BUFF_SIZE);
	confitems.work_threads = cfg_get_intvalue(&cfg_hashtable,"work.threads",DEFAULT_THREADS_NUMBER);
	confitems.heart_beat_interval = cfg_get_intvalue(&cfg_hashtable,"heart.beat.interval",DEFAULT_HEART_BEAT_INTERVAL);
	confitems.snapshot_thread_interval = cfg_get_intvalue(&cfg_hashtable,"snapshot.thread.interval",DEFAULT_SNAPSHOT_THREAD_INTERVAL);
	return LFS_OK;
}

void CTXS_LOCK()
{
	pthread_mutex_lock(&ctxs_lock);
}

void CTXS_UNLOCK()
{
	pthread_mutex_unlock(&ctxs_lock);
}

void MEM_FILE_LOCK()
{
	pthread_mutex_lock(&mem_file_lock);
}

void MEM_FILE_UNLOCK()
{
	pthread_mutex_unlock(&mem_file_lock);
}
