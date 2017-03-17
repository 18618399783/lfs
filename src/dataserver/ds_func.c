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
#include "sched_timer.h"
#include "shared_func.h"
#include "sock_opt.h"
#include "ds_queue.h"
#include "ds_conn.h"
#include "ds_network_io.h"
#include "ds_client_network_io.h"
#include "ds_binlog.h"
#include "ds_types.h"
#include "ds_block.h"
#include "ds_sync.h"
#include "ds_file.h"
#include "ds_disk_io.h"
#include "ds_func.h"

#define CTX_SNAPSHOT_FILENAME "ctxsnapshot.bat"
#define CTX_SNAPSHOT_DATAFIELDS 3

static pthread_mutex_t ctxs_lock = PTHREAD_MUTEX_INITIALIZER;

void settings_init(void)
{
	settings.backlog = DEFAULT_LISTEN_BACKLOG;
	settings.cfgfilepath = NULL;
}

void ctxs_init(void)
{
	int next_fd = dup(1);

	ctxs.max_fds = confitems.max_connections + next_fd;
	close(next_fd);

	ctxs.curr_conns = 0;
   	ctxs.total_conns = 0;
	ctxs.rejected_conns = 0;
	ctxs.reserved_fds = 0;
	ctxs.started_time = time(NULL);
	ctxs.register_timestamp = 0;
	ctxs.last_sync_sequence = 0;
	ctxs.is_fullsyncdone = false;
	ctxs.last_mount_block_index = 0;
	ctxs.block_opt_count = 0;
	ctxs.sid = 0;
	char *local_addr = cfg_get_strvalue(&cfg_hashtable,"bind.addr");
	if( local_addr != NULL)
	{
		ctxs.sid = hash_func((const void*)local_addr,\
				(size_t)strlen(local_addr));
	}
	base64_init_ex(&ctxs.b64_ctx,0,'-','_','.');
}

void conf_items_init(void)
{
	confitems.logger_level = "INFO";
	confitems.logger_file_name = "ds";
	confitems.bind_addr = LOCALHOST;
	confitems.bind_port = DEFAULT_PORT;
	confitems.network_timeout = DEFAULT_NETWORK_TIMEOUT;
	confitems.heart_beat_interval = DEFAULT_HEART_BEAT_INTERVAL;
	confitems.sync_report_interval = DEFAULT_SYNC_INTERVAL;
	confitems.sync_wait_time = DEFAULT_SYNC_WAIT_TIME_MSEC;
	confitems.sync_interval = DEFAULT_SYNC_INTERVAL;
	confitems.binlog_flush_interval = DEFAULT_BINLOG_FLUSH_INTERVAL;
	confitems.state_report_interval = DEFAULT_STATE_REPORT_INTERVAL;
	confitems.block_vfs_interval = DEFAULT_BLOCK_VFS_INTERVAL;
	confitems.block_snapshot_interval = DEFAULT_BLOCK_SNAPSHOT_INTERVAL;
	confitems.max_connections = DEFAULT_MAXCONNS;
	confitems.thread_stack_size = DEFAULT_THREAD_STACK_SIZE;
	confitems.nio_read_buffer_size = DEFAULT_NIO_READ_BUFF_SIZE;
	confitems.nio_write_buffer_size = DEFAULT_NIO_WRITE_BUFF_SIZE;
	confitems.file_buffer_size = DEFAULT_FILE_BUFF_SIZE; 
	confitems.nio_threads = DEFAULT_NIO_THREADS_NUMBER;
	confitems.dio_read_threads = DEFAULT_DIO_READ_THREADS_NUMBER; 
	confitems.dio_write_threads = DEFAULT_DIO_WRITE_THREADS_NUMBER; 
	confitems.block_count = DEFAULT_BLOCK_COUNT;
	confitems.block_sub_count = DEFAULT_BLOCK_SUB_COUNT;
	confitems.block_max_file_count = DEFAULT_BLOCK_MAX_FILE_COUNT;
	confitems.block_free_size_ratio = DEFAULT_BLOCK_FREE_SIZE_RATIO;
	confitems.block_mount = NULL;
	confitems.weight = DEFAULT_WEIGHT; 
	confitems.file_fsync_written_bytes = DEFAULT_FILE_FSYNC_WRITTEN_BYTES;
}

void dataserver_destroy()
{
	cfg_destroy(&cfg_hashtable);
	logger_destroy();	
	sched_timer_exit();
	file_ctx_mpools_destroy();
	nio_threads_destroy();
	dio_thread_pool_destroy();
	binlog_destroy();
	async_thread_destroy();
}

int set_cfg2globalobj(void)
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
	pbp = cfg_get_strvalue(&cfg_hashtable,"group.name");
	if(pbp == NULL)
	{
		fprintf(stderr,"The cfg file must include the \"group.name\"items!\n");
		return LFS_ERROR;
	}
	confitems.group_name = pbp;
	pbp = cfg_get_strvalue(&cfg_hashtable,"volume.name");
	if(pbp == NULL)
	{
		fprintf(stderr,"The cfg file must include the \"volume.name\"items!\n");
		return LFS_ERROR;
	}
	confitems.volume_name = pbp;
	pbp = cfg_get_strvalue(&cfg_hashtable,"block.mount");
	if(pbp == NULL)
	{
		fprintf(stderr,"The cfg file must include the \"block.mount\"items!\n");
		return LFS_ERROR;
	}
	confitems.block_mount = pbp;
	pbp = cfg_get_strvalue(&cfg_hashtable,"tracker.servers");
	if(pbp == NULL)
	{
		fprintf(stderr,"The cfg file must include the \"tracker.servers\"items!\n");
		return LFS_ERROR;
	}
	confitems.tracker_servers = pbp;

	confitems.logger_level = cfg_get_strvalue(&cfg_hashtable,"logger.level");
	confitems.logger_file_name = cfg_get_strvalue(&cfg_hashtable,"logger.file.name");
	confitems.bind_addr = cfg_get_strvalue(&cfg_hashtable,"bind.addr");
	confitems.bind_port = cfg_get_intvalue(&cfg_hashtable,"bind.port",DEFAULT_PORT);
	confitems.network_timeout = cfg_get_intvalue(&cfg_hashtable,"network.timeout",DEFAULT_NETWORK_TIMEOUT);
	confitems.heart_beat_interval = cfg_get_intvalue(&cfg_hashtable,"heart.beat.interval",DEFAULT_HEART_BEAT_INTERVAL);
	confitems.sync_report_interval = cfg_get_intvalue(&cfg_hashtable,"sync.report.interval",DEFAULT_SYNC_INTERVAL);
	confitems.sync_wait_time = cfg_get_intvalue(&cfg_hashtable,"sync.wait.time",DEFAULT_SYNC_WAIT_TIME_MSEC);
	confitems.sync_interval = cfg_get_intvalue(&cfg_hashtable,"sync.interval",DEFAULT_SYNC_INTERVAL);
	confitems.binlog_flush_interval = cfg_get_intvalue(&cfg_hashtable,"binlog.flush.interval",DEFAULT_BINLOG_FLUSH_INTERVAL);
	confitems.state_report_interval = cfg_get_intvalue(&cfg_hashtable,"state.report.interval",DEFAULT_STATE_REPORT_INTERVAL);
	confitems.block_snapshot_interval = cfg_get_intvalue(&cfg_hashtable,"block.snapshot.interval",DEFAULT_BLOCK_SNAPSHOT_INTERVAL);
	confitems.block_vfs_interval = cfg_get_intvalue(&cfg_hashtable,"block.vfs.interval",DEFAULT_BLOCK_VFS_INTERVAL);
	confitems.max_connections = cfg_get_intvalue(&cfg_hashtable,"max.connections",DEFAULT_MAXCONNS);
	confitems.thread_stack_size = cfg_get_intvalue(&cfg_hashtable,"thread.stack.size",DEFAULT_THREAD_STACK_SIZE);
	confitems.nio_read_buffer_size = cfg_get_intvalue(&cfg_hashtable,"nio.read.buffer.size",DEFAULT_NIO_READ_BUFF_SIZE);
	confitems.nio_write_buffer_size = cfg_get_intvalue(&cfg_hashtable,"nio.write.buffer.size",DEFAULT_NIO_WRITE_BUFF_SIZE);
	confitems.file_buffer_size = cfg_get_intvalue(&cfg_hashtable,"file.buffer.size",DEFAULT_FILE_BUFF_SIZE);
	confitems.nio_threads = cfg_get_intvalue(&cfg_hashtable,"nio.threads",DEFAULT_NIO_THREADS_NUMBER);
	confitems.dio_read_threads = cfg_get_intvalue(&cfg_hashtable,"dio.read.threads",DEFAULT_DIO_READ_THREADS_NUMBER);
	confitems.dio_write_threads = cfg_get_intvalue(&cfg_hashtable,"dio.write.threads",DEFAULT_DIO_WRITE_THREADS_NUMBER);
	confitems.block_sub_count = cfg_get_intvalue(&cfg_hashtable,"block.sub.count",DEFAULT_BLOCK_SUB_COUNT);
	confitems.block_max_file_count = cfg_get_intvalue(&cfg_hashtable,"block.max.file.count",DEFAULT_BLOCK_MAX_FILE_COUNT);
	confitems.block_free_size_ratio = cfg_get_floatvalue(&cfg_hashtable,"block.free.size.ratio",DEFAULT_BLOCK_FREE_SIZE_RATIO);
	confitems.weight = cfg_get_intvalue(&cfg_hashtable,"weight",DEFAULT_WEIGHT);
	confitems.file_fsync_written_bytes = cfg_get_intvalue(&cfg_hashtable,"file.fsync.written.bytes",DEFAULT_FILE_FSYNC_WRITTEN_BYTES);
	return LFS_OK;
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

void CTXS_LOCK()
{
	pthread_mutex_lock(&ctxs_lock);
}

void CTXS_UNLOCK()
{
	pthread_mutex_unlock(&ctxs_lock);
}

bool is_local_block(const char *ipaddr)
{
	if(strcmp(ipaddr,confitems.bind_addr) == 0)
	{
		return true;
	}	
	return false;
}

int ctx_snapshot_batdata_load(void)
{
	char fname[MAX_PATH_SIZE];
	FILE *fp;
	char sline[BINLOG_BAT_LINE_BUFF_SIZE];
	char *fields[CTX_SNAPSHOT_DATAFIELDS];
	int col_count;

	snprintf(fname,sizeof(fname),"%s/sync/%s",\
			base_path,CTX_SNAPSHOT_FILENAME);
	if((fp = fopen(fname,"r")) == NULL)	
	{
		logger_error("file: "__FILE__", line: %d," \
				"Open file \"%s\",errno:%d," \
				"error info:%s!", __LINE__,fname,errno,strerror(errno));
		return errno;
	}
	while(fgets(sline,sizeof(sline),fp) != NULL)
	{
		if(*sline == '\0')
		{
			continue;
		}
		col_count = splitStr(sline,BAT_DATA_SEPERATOR_SPLITSYMBOL,\
				fields,CTX_SNAPSHOT_DATAFIELDS);
		if(col_count != CTX_SNAPSHOT_DATAFIELDS && \
				col_count != CTX_SNAPSHOT_DATAFIELDS - 8)
		{
			logger_error("file: "__FILE__", line: %d," \
					"The snapshot of the file  \"%s\" is invalid!",\
				   	__LINE__,fname);
			break;
		}
		ctxs.register_timestamp = (time_t)atol(trim(fields[0]));
		ctxs.is_fullsyncdone = (bool)atoi(trim(fields[1]));
		ctxs.last_sync_sequence = (int64_t)atol(trim(fields[2]));
	}
	fclose(fp);

	return LFS_OK;
}

int ctx_snapshot_batdata_flush()
{
	char fname[MAX_PATH_SIZE];
	char buff[BINLOG_BAT_LINE_BUFF_SIZE];
	int fd;
	int len;

	snprintf(fname,sizeof(fname),"%s/sync/%s",\
			base_path,CTX_SNAPSHOT_FILENAME);
	if((fd = open(fname,O_WRONLY|O_CREAT|O_TRUNC,0644)) < 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Open \"%s\" failed,errno:%d," \
				"error info:%s!", __LINE__,fname,errno,strerror(errno));
		return errno;	
	}
	len = sprintf(buff,\
			"%ld%c%d%c%ld\n",\
			(long int)ctxs.register_timestamp,\
			BAT_DATA_SEPERATOR_SPLITSYMBOL,\
			ctxs.is_fullsyncdone,\
			BAT_DATA_SEPERATOR_SPLITSYMBOL,\
			(int64_t)ctxs.last_sync_sequence);
	if(write(fd,buff,len) != len)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Write  sync ini data to file  \"%s\" failed,errno:%d," \
				"error info:%s!", __LINE__,fname,errno,strerror(errno));
		close(fd);
		return errno;	
	}
	close(fd);
	return LFS_OK;
}

