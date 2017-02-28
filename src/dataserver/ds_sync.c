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
#include <errno.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>

#include "logger.h"
#include "shared_func.h"
#include "sock_opt.h"
#include "ds_func.h"
#include "ds_types.h"
#include "ds_binlog.h"
#include "ds_block.h"
#include "ds_client_network_io.h"
#include "ds_file.h"
#include "ds_sync.h"

#define FULL_SYNC_NETWORK_ERROR 1
#define WRITE_TO_BAT_FILE_BY_SYNC_COUNT 2 
#define FULL_SYNC_BINLOG_MARK_FILENAME "local_sync.mark"
#define FULL_SYNC_BINLOG_MARK_DATAFIELDS 5

static pthread_t *async_tids = NULL;
int async_threads_count = 0;
static pthread_mutex_t async_thread_lock;
volatile int do_run_async_thread = 1;

static void* __async_thread_entry(void *arg); 
static int __async_handle(int sfd,sync_ctx *sctx,block_brief *bbrief,binlog_record *brecord);
static int __async_copy_file(int sfd,sync_ctx *sctx,block_brief *bbrief,binlog_record *brecord);

static enum full_sync_state __full_sync_binlog_mark_initload(full_sync_binlog_mark *fmark);
static enum full_sync_state __full_sync_binlog_mark_write(full_sync_binlog_mark *fmark);
static enum full_sync_state __full_sync_binlog_from_master(connect_info *cinfo,full_sync_binlog_mark *fmark);
static enum full_sync_state __full_sync_data_from_master(connect_info *cinfo,sync_ctx *sctx);
static enum full_sync_state __binlogmete_get_from_master(connect_info *cinfo,full_sync_binlog_mark *fmark);
static enum full_sync_state __sync_local_binlog_data_from_master(connect_info *cinfo,full_sync_binlog_mark *fmark);
static enum full_sync_state __sync_remote_binlog_data_from_master(connect_info *cinfo,full_sync_binlog_mark *fmark);
static enum full_sync_state __full_sync_append_remote_binlog(connect_info *cinfo,full_sync_binlog_mark *fmark,const int64_t bfile_size);
static enum full_sync_state __full_sync_handle(connect_info *cinfo,sync_ctx *sctx,binlog_record *brecord);

enum full_sync_state full_sync_from_master(connect_info *cinfo)
{
	assert(cinfo != NULL);
	enum full_sync_state fstate = F_SYNC_OK;
	int nfaild_count = 0;
	int do_full_sync_flag = 1;
	full_sync_binlog_mark fmark;

	sync_ctx sctx;
	cinfo->sfd = -1;
	memset(&sctx,0,sizeof(sync_ctx));
	memset(&fmark,0,sizeof(full_sync_binlog_mark));
	
	while(do_full_sync_flag)
	{
		cinfo->sfd = socket(AF_INET,SOCK_STREAM,0);
		if(cinfo->sfd < 0)
		{
			logger_error("file: "__FILE__", line: %d, " \
					"Failed to create socket,errno:%d," \
					"error info:%s!", __LINE__,errno,strerror(errno));
			return F_SYNC_NETWORK_ERROR;
		}
#if 0
		if(socket_bind(cinfo->sfd,NULL,0) != 0)
		{
			fstate = F_SYNC_NETWORK_ERROR;
			goto err;
		}
#endif
		if(set_noblock(cinfo->sfd) != 0)
		{
			fstate = F_SYNC_NETWORK_ERROR;
			goto err;
		}
		if(connectserver_nb(cinfo->sfd,cinfo->ipaddr,cinfo->port,confitems.network_timeout) != 0)
		{
			logger_error("file: "__FILE__", line: %d, " \
					"Failed to connect master server %s:%d,errno:%d,"\
					"error info:%s!",\
					__LINE__,cinfo->ipaddr,cinfo->port,errno,strerror(errno));
			if(nfaild_count > 3)
			{
				logger_error("file: "__FILE__", line: %d, " \
						"Connect master server %s:%d failed %d count,quit full sync.",\
						__LINE__,cinfo->ipaddr,cinfo->port,nfaild_count);
				fstate = F_SYNC_NETWORK_ERROR;
				goto err;
			}
			if(do_full_sync_flag)
			{
				nfaild_count++;
				close(cinfo->sfd);
				cinfo->sfd = -1;
				CTXS_LOCK();
				ctxs.rejected_conns++;
				CTXS_UNLOCK();
				sleep(3);
				continue;
			}
		}
		CTXS_LOCK();
		ctxs.curr_conns++;
		ctxs.total_conns++;
		CTXS_UNLOCK();
		logger_info("Successfully connect to master server %s:%d,starting full sync." \
				,cinfo->ipaddr,cinfo->port);
		fstate = __full_sync_binlog_from_master(cinfo,&fmark);
		if((fstate == F_SYNC_ERROR) || \
				(fstate == F_SYNC_MARK_ERROR))
		{
			goto err;
		}
		else if(fstate == F_SYNC_NETWORK_ERROR)
		{
			close(cinfo->sfd);
			cinfo->sfd = -1;
			CTXS_LOCK();
			ctxs.curr_conns--;
			CTXS_UNLOCK();
			continue;
		}
		if((fmark.b_file_count + fmark.rb_file_count) > 0)
		{
			fstate = __full_sync_data_from_master(cinfo,&sctx);
			if(fstate == F_SYNC_FINISH)
			{
				break;
			}
			else if(fstate == F_SYNC_ERROR)
			{
				goto err;
			}
			else if(fstate == F_SYNC_NETWORK_ERROR)
			{
				close(cinfo->sfd);
				cinfo->sfd = -1;
				CTXS_LOCK();
				ctxs.curr_conns--;
				CTXS_UNLOCK();
				continue;
			}
		}
		else
		{
			fstate = F_SYNC_FINISH;
			break;
		}
	}
err:
	if(cinfo->sfd >= 0)
	{
		close(cinfo->sfd);
		cinfo->sfd = -1;
		CTXS_LOCK();
		ctxs.curr_conns--;
		CTXS_UNLOCK();
	}
	return fstate;
}

int async_thread_init(void)
{
	async_tids = NULL;
	async_threads_count = 0;
	char fpath[MAX_PATH_SIZE];

	snprintf(fpath,sizeof(fpath),"%s/sync",base_path);
	if(!isDir(fpath))
	{
		if(mkdir(fpath,0755) != 0)
		{
			logger_error("file: "__FILE__", line: %d," \
					"Mkdir \"%s\" failed,errno:%d," \
					"error info:%s!", __LINE__,fpath,errno,strerror(errno));
			return LFS_ERROR;
		}
	}
	pthread_mutex_init(&async_thread_lock,NULL);
	return LFS_OK;
}

int async_thread_destroy(void)
{
	do_run_async_thread = 0;
	if(async_tids != NULL)
	{
		free(async_tids);
		async_tids = NULL;
		async_threads_count = 0;
	}
	pthread_mutex_destroy(&async_thread_lock);
	return LFS_OK;
}

int async_thread_quit(void)
{
	do_run_async_thread = 0;
	if(async_tids != NULL)
	{
		free(async_tids);
		async_tids = NULL;
		async_threads_count = 0;
	}
	return LFS_OK;
}

int async_thread_start(block_brief *bbrief)
{
	int ret;
	pthread_attr_t attr;
	pthread_t tid;

	if((bbrief->state != active) && (bbrief->state != on_line))
	{
		logger_warning("file: "__FILE__", line: %d," \
				"The server(%s:%d)'s state(%d) is invalid!",\
				__LINE__,\
				bbrief->ipaddr,\
				bbrief->port,\
				bbrief->state);
		return LFS_OK;
	}
	if(is_local_block(bbrief->ipaddr))
	{
		logger_warning("file: "__FILE__", line: %d," \
				"The server(%s:%d)'s myself,don't create async thread!",\
				__LINE__,\
				bbrief->ipaddr,\
				bbrief->port);
		return LFS_OK;
	}
	if((ret = init_pthread_attr(&attr,confitems.thread_stack_size)) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"Failed init pthread attr: %s", __LINE__,strerror(ret));
		return ret;
	}
	if((ret = pthread_create(&tid,&attr,__async_thread_entry,(void*)bbrief)) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"Can't create async thread for server(%s:%d),errno:%d,error info: %s.",\
				   	__LINE__,\
					bbrief->ipaddr,\
					bbrief->port,\
					errno,\
					strerror(ret));
		pthread_attr_destroy(&attr);
		return ret;
	}
	pthread_mutex_lock(&async_thread_lock);
	async_threads_count++;
	async_tids = (pthread_t*)realloc(async_tids,sizeof(pthread_t) * async_threads_count);
	if(async_tids == NULL)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"Allocate memeory to async threads failed,errno:%d,error info: %s.",\
				   	__LINE__,\
					errno,\
					strerror(ret));
		pthread_mutex_unlock(&async_thread_lock);
		pthread_attr_destroy(&attr);
		async_threads_count--;
		return LFS_ERROR;
	}
	async_tids[async_threads_count - 1] = tid;
	pthread_mutex_unlock(&async_thread_lock);
	pthread_attr_destroy(&attr);
	return LFS_OK;
}

int async_thread_exit(pthread_t *tid,block_brief *bbrief)
{
	assert((tid != NULL) && (bbrief != NULL));
	int i;

	pthread_mutex_lock(&async_thread_lock);
	for(i = 0; i < async_threads_count; i++)
	{
		if(pthread_equal(async_tids[i],*tid))
		{
			break;
		}
	}
	while(i < async_threads_count - 1)
	{
		async_tids[i] = async_tids[i + 1];
		i++;
	}	
	async_threads_count--;
	pthread_mutex_unlock(&async_thread_lock);
	logger_info("file: "__FILE__", line: %d, " \
			"Async thread to server(%s:%d) exit.", \
			__LINE__,\
			bbrief->ipaddr,\
			bbrief->port);
	return LFS_OK;
}

static void* __async_thread_entry(void *arg)
{
	int ret;
	int nfaildcount;
	int sfd;
	pthread_t tid;
	block_brief *bbrief;
	sync_ctx sctx;
	binlog_record brecord;
	int record_length;
	char faild_info[64] = {0};
	enum binlog_file_state bstate;

	nfaildcount = 0;
	sfd = -1;
	tid = pthread_self();
	bbrief = (block_brief*)arg;

	while((do_run_async_thread) && (bbrief->state != deleted))
	{
		if((bbrief->state == init) || \
				(bbrief->state == wait_sync) || \
				(bbrief->state == syncing) || \
				(bbrief->state == off_line))
		{
			usleep(5);
			continue;
		}
		if((!do_run_async_thread) || \
				bbrief->state == deleted)
		{
			break;
		}
		nfaildcount = 0;
		while((do_run_async_thread) && (bbrief->state != deleted))
		{
			sfd = socket(AF_INET,SOCK_STREAM,0);
			if(sfd < 0)
			{
				logger_error("file: "__FILE__", line: %d, " \
						"Failed to create socket,errno:%d," \
						"error info:%s!", __LINE__,errno,strerror(errno));
				do_run_async_thread = false;
				break;
			}
			socket_bind(sfd,NULL,0);
			set_noblock(sfd);
			set_sock_opt(sfd,confitems.network_timeout);
			if(connectserver_nb(sfd,bbrief->ipaddr,bbrief->port,confitems.network_timeout) != 0)
			{
				logger_error("file: "__FILE__", line: %d, " \
						"Failed to connect the slave server %s:%d,errno:%d," \
						"error info:%s!",\
						__LINE__,\
						bbrief->ipaddr,\
						bbrief->port,\
						errno,\
						strerror(errno));
				if(do_run_async_thread)
				{
					nfaildcount++;
					close(sfd);
					sfd = -1;
					CTXS_LOCK();
					ctxs.rejected_conns++;
					CTXS_UNLOCK();
					usleep(confitems.heart_beat_interval);
					continue;
				}
			}
			if(nfaildcount == 0)
			{
				*faild_info = '\0';
			}
			else
			{
				sprintf(faild_info,",try connect count:%d",nfaildcount);
			}
			CTXS_LOCK();
			ctxs.curr_conns++;
			ctxs.total_conns++;
			CTXS_UNLOCK();
			logger_info("file: "__FILE__", line: %d, " \
					"Successfully connect the slave server %s:%d%s",\
					__LINE__,\
					bbrief->ipaddr,\
					bbrief->port,\
					faild_info);
			if((!do_run_async_thread) || \
					bbrief->state == deleted)
			{
				break;
			}
			if((bbrief->state != active) && \
					(bbrief->state != on_line))
			{
				close(sfd);
				sfd = -1;
				CTXS_LOCK();
				ctxs.curr_conns--;
				CTXS_UNLOCK();
				usleep(5);
				continue;
			}
			if((ret = asyncctx_init(bbrief,&sctx,&bctx)) != 0)
			{
				logger_crit("file: "__FILE__", line: %d, " \
						"Init slave server %s:%d's binlog bat context failed.",\
						__LINE__,\
						bbrief->ipaddr,\
						bbrief->port);
				do_run_async_thread = 0;
				break;
			}
			while((do_run_async_thread) && \
					(bbrief->state == on_line || \
					 bbrief->state == active))
			{
				if((bstate = binlog_read(&sctx,&bctx,&brecord,&record_length)) == B_FILE_OK)
				{
					if((ret = __async_handle(sfd,&sctx,bbrief,&brecord)) != LFS_OK)
					{
						logger_error("file: "__FILE__", line: %d, " \
								"Binlog file index of %d file id:%s async to slave server %s:%d failed.",\
								__LINE__,\
								sctx.b_index,\
								brecord.f_id,\
								bbrief->ipaddr,\
								bbrief->port);
						break;
					}
				}
				else if(bstate == B_FILE_NODATA)
				{
					usleep(confitems.sync_wait_time);
					continue;
				}
				else if(bstate == B_FILE_ERROR)
				{
					usleep(1);
					break;
				}
				sctx.b_offset += record_length;
				sctx.sync_count++;
				sctx.last_timestamp = time(NULL);
				if((sctx.sync_count - sctx.last_sync_count) >= \
						WRITE_TO_BAT_FILE_BY_SYNC_COUNT)
				{
					if((ret = sync_mark_file_batdata_write(&sctx)) != 0)
					{
						logger_error("file: "__FILE__", line: %d, " \
								"Write slave server %s:%d's binlog bat data file failed.",\
								__LINE__,\
								bbrief->ipaddr,\
								bbrief->port);
						do_run_async_thread = 0;
						break;
					}
				}
				if(confitems.sync_interval > 0)
				{
					usleep(confitems.sync_interval);
				}
			}
			if(sctx.last_sync_count != sctx.sync_count)
			{
				if((ret = sync_mark_file_batdata_write(&sctx)) != 0)
				{
					logger_crit("file: "__FILE__", line: %d, " \
							"Write slave server %s:%d's binlog bat data file failed.",\
							__LINE__,\
							bbrief->ipaddr,\
							bbrief->port);
					do_run_async_thread = 0;
					break;
				}
			}
			if(sfd >= 0)
			{
				close(sfd);
				sfd = -1;
				CTXS_LOCK();
				ctxs.curr_conns--;
				CTXS_UNLOCK();
			}
			binlogsyncctx_destroy(&sctx);
		}
		if(sfd >= 0)
		{
			close(sfd);
			sfd = -1;
			CTXS_LOCK();
			ctxs.curr_conns--;
			CTXS_UNLOCK();
		}
	}
	if(sfd >= 0)
	{
		close(sfd);
		sfd = -1;
		CTXS_LOCK();
		ctxs.curr_conns--;
		CTXS_UNLOCK();
	}
	binlogsyncctx_destroy(&sctx);
	async_thread_exit(&tid,bbrief);
	return NULL;
}

static int __async_handle(int sfd,sync_ctx *sctx,block_brief *bbrief,binlog_record *brecord)
{
	int ret = LFS_OK;
	if(sfd < 0)
		return LFS_ERROR;

	switch(brecord->op_type)
	{
		case FILE_OP_TYPE_CREATE_FILE:
			ret = __async_copy_file(sfd,sctx,bbrief,brecord);
			break;
		default:
			logger_warning("file: "__FILE__", line: %d, " \
					"Binlog file index of %d File id: %s record operation type is invalid.",\
					__LINE__,\
					sctx->b_index,\
					brecord->f_id);
			return LFS_ERROR;		
	}
	return ret;
}

static int __async_copy_file(int sfd,sync_ctx *sctx,block_brief *bbrief,binlog_record *brecord)
{
	int ret = LFS_OK;
	protocol_header *req_header;
	int64_t body_len;
	int64_t rbytes;
	struct stat stat_buff;
	char req_buff[sizeof(protocol_header) + LFS_STRUCT_PROP_LEN_SIZE8  * 3 + LFS_FILE_ID_SIZE];
	char *p;

	if((ret = stat(brecord->f_block_map_name,&stat_buff)) != 0)
	{
		if(ret == ENOENT)
		{
			logger_warning("file: "__FILE__", line: %d, " \
					"The file id: %s file does not exists.",\
					__LINE__,\
					brecord->f_id);
			return 0;
		}
		else
		{
			logger_error("file: "__FILE__", line: %d," \
					"Get file \"%s\" stat failed,errno:%d," \
					"error info:%s!", __LINE__,brecord->f_block_map_name,errno,strerror(errno));
			return errno;
		}
	}
	memset(req_buff,0,sizeof(req_buff));
	do
	{
		body_len = LFS_STRUCT_PROP_LEN_SIZE8 * 3 + brecord->f_id_length;  
		req_header = (protocol_header*)req_buff;
		req_header->header_s.body_len = body_len;
		req_header->header_s.cmd = PROTOCOL_CMD_ASYNC_COPY_FILE; 
		p = req_buff + sizeof(protocol_header);
		long2buff(brecord->sequence,p);
		p += LFS_STRUCT_PROP_LEN_SIZE8;
		long2buff(brecord->f_id_length,p);
		p += LFS_STRUCT_PROP_LEN_SIZE8; 
		memcpy(p,brecord->f_id,brecord->f_id_length);
		p += brecord->f_id_length;
		long2buff(stat_buff.st_size,p);
		p += LFS_STRUCT_PROP_LEN_SIZE8; 
		if((ret = senddata_nblock(sfd,(void*)req_buff,(p - req_buff),confitems.network_timeout)) != 0)
		{
			logger_error("file: "__FILE__", line: %d," \
					"Async data to slave block(%s:%d) failed,errno:%d," \
					"error info:%s!",\
				   	__LINE__,\
					bbrief->ipaddr,\
					bbrief->port,\
					errno,strerror(errno));
			break;
		}
		if((ret = client_sendfile(sfd,(const char*)brecord->f_block_map_name,\
						0,(const int64_t)stat_buff.st_size,confitems.network_timeout)) != 0)
		{
			logger_error("file: "__FILE__", line: %d," \
					"Async data to slave block(%s:%d) failed,errno:%d," \
					"error info:%s!",\
				   	__LINE__,\
					bbrief->ipaddr,\
					bbrief->port,\
					errno,strerror(errno));
			break;
		}
		if((ret = client_recvheader(sfd,&rbytes)) != 0)
		{
			break;
		}
	}while(0);
	return ret;
}

static enum full_sync_state __full_sync_binlog_mark_initload(full_sync_binlog_mark *fmark)
{
	assert(fmark != NULL);
	char fname[MAX_PATH_SIZE];
	FILE *fp;
	char sline[BINLOG_BAT_LINE_BUFF_SIZE];
	char *fields[FULL_SYNC_BINLOG_MARK_DATAFIELDS];
	int col_count;

	snprintf(fname,sizeof(fname),"%s/sync/%s",\
			base_path,FULL_SYNC_BINLOG_MARK_FILENAME);
	if(fileExists(fname))
	{
		if((fp = fopen(fname,"r")) == NULL)	
		{
			logger_error("file: "__FILE__", line: %d," \
					"Open full sync mark file \"%s\",errno:%d," \
					"error info:%s!", __LINE__,fname,errno,strerror(errno));
			return F_SYNC_MARK_ERROR;
		}
		while(fgets(sline,sizeof(sline),fp) != NULL)
		{
			if(*sline == '\0')
			{
				continue;
			}
			col_count = splitStr(sline,BAT_DATA_SEPERATOR_SPLITSYMBOL,\
					fields,FULL_SYNC_BINLOG_MARK_DATAFIELDS);
			if(col_count != FULL_SYNC_BINLOG_MARK_DATAFIELDS)
			{
				logger_error("file: "__FILE__", line: %d," \
						"The full sync of the mark file  \"%s\" is invalid!",\
						__LINE__,fname);
				break;
			}
			fmark->b_file_count = atoi(trim(fields[0]));
			fmark->b_curr_sync_index = atoi(trim(fields[1]));
			fmark->rb_file_count = atoi(trim(fields[2]));
			fmark->rb_curr_sync_index = atoi(trim(fields[3]));
			fmark->last_sync_timestamp = atol(trim(fields[4]));
		}
		fclose(fp);
	}
	return F_SYNC_OK;
}

static enum full_sync_state __full_sync_binlog_mark_write(full_sync_binlog_mark *fmark)
{
	assert(fmark != NULL);
	char fname[MAX_PATH_SIZE];
	char buff[BINLOG_BAT_LINE_BUFF_SIZE];
	int fd;
	int len;

	snprintf(fname,sizeof(fname),"%s/sync/%s",\
			base_path,FULL_SYNC_BINLOG_MARK_FILENAME);
	if((fd = open(fname,O_WRONLY|O_CREAT|O_TRUNC,0644)) < 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Open full sync mark file \"%s\" failed,errno:%d," \
				"error info:%s!", __LINE__,fname,errno,strerror(errno));
		return F_SYNC_MARK_ERROR;	
	}
	len = sprintf(buff,\
			"%d%c%d%c%d%c%d%c%ld\n",\
			fmark->b_file_count,\
			BAT_DATA_SEPERATOR_SPLITSYMBOL,\
			fmark->b_curr_sync_index,\
			BAT_DATA_SEPERATOR_SPLITSYMBOL,\
			fmark->rb_file_count,\
			BAT_DATA_SEPERATOR_SPLITSYMBOL,\
			fmark->rb_curr_sync_index,\
			BAT_DATA_SEPERATOR_SPLITSYMBOL,\
			fmark->last_sync_timestamp);
	if(write(fd,buff,len) != len)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Write  full sync mark to file  \"%s\" failed,errno:%d," \
				"error info:%s!", __LINE__,fname,errno,strerror(errno));
		close(fd);
		return F_SYNC_MARK_ERROR;	
	}
	close(fd);
	return F_SYNC_OK;
}

static enum full_sync_state __full_sync_binlog_from_master(connect_info *cinfo,full_sync_binlog_mark *fmark)
{
	assert((cinfo != NULL) && (fmark != NULL));
	enum full_sync_state fstate = F_SYNC_FINISH;
	int i;

	if((fstate = __full_sync_binlog_mark_initload(fmark)) != F_SYNC_OK)
	{
		return fstate;
	}
	if((fmark->b_file_count + fmark->rb_file_count) == 0)
	{
		if((fstate = __binlogmete_get_from_master(cinfo,fmark)) != F_SYNC_OK)
		{
			return fstate;
		}
	}
	if((fmark->rb_file_count > 0) && \
			(fmark->rb_file_count != fmark->rb_curr_sync_index))
	{
		for(i = fmark->rb_curr_sync_index; \
				i < fmark->rb_file_count; i++)
		{
			if((fstate = __sync_remote_binlog_data_from_master(cinfo,fmark)) != F_SYNC_OK)
			{
				return fstate;
			}
		}
	}
	i = 0;
	if((fmark->b_file_count > 0) && \
			(fmark->b_file_count != fmark->b_curr_sync_index))
	{
		for(i = fmark->b_curr_sync_index; \
				i < fmark->b_file_count; i++)
		{
			if((fstate = __sync_local_binlog_data_from_master(cinfo,fmark)) != F_SYNC_OK)
			{
				return fstate;
			}
		}
	}
	return fstate;
}

static enum full_sync_state __full_sync_data_from_master(connect_info *cinfo,sync_ctx *sctx)
{
	int ret;
	int do_full_sync_data = 1;
	binlog_record brecord;
	int brecord_size;
	enum binlog_file_state bstate;
	enum full_sync_state fstate = F_SYNC_OK;

	if((ret = fullsyncctx_init(sctx,&rbctx,cinfo)) != 0)
	{
		logger_crit("file: "__FILE__", line: %d, " \
				"Init local full sync binlog context failed.",\
				__LINE__);
		return F_SYNC_ERROR;
	}
	while(do_full_sync_data)
	{
		if((bstate = binlog_read(sctx,&rbctx,&brecord,&brecord_size)) == B_FILE_OK)
		{
			if((fstate = __full_sync_handle(cinfo,sctx,&brecord)) != F_SYNC_OK)
			{
				logger_error("file: "__FILE__", line: %d, " \
						"Binlog file index of %d and file id:%s "\
						"full sync data from master server %s:%d failed.",\
						__LINE__,\
						sctx->b_index,\
						brecord.f_id,\
						cinfo->ipaddr,\
						cinfo->port);
				break;
			}
		}
		else if(bstate == B_FILE_NODATA)
		{
			fstate = F_SYNC_FINISH;
			break;
		}
		else if(bstate == B_FILE_ERROR)
		{
			fstate = F_SYNC_ERROR;
			break;
		}
		sctx->b_offset += brecord_size;
		sctx->sync_count++;
		sctx->last_timestamp = time(NULL);
		if(brecord.sequence > ctxs.last_sync_sequence)
		{
			ctxs.last_sync_sequence = brecord.sequence;
		}
		if((sctx->sync_count - sctx->last_sync_count) >= \
				WRITE_TO_BAT_FILE_BY_SYNC_COUNT)
		{
			if((ret = sync_mark_file_batdata_write(sctx)) != 0)
			{
				logger_crit("file: "__FILE__", line: %d, " \
						"Write local full sync binlog context data file failed.",\
						__LINE__);
				do_run_async_thread = 0;
				return F_SYNC_ERROR;
			}
		}
	}
	if(sctx->last_sync_count != sctx->sync_count)
	{
		if((ret = sync_mark_file_batdata_write(sctx)) != 0)
		{
			logger_crit("file: "__FILE__", line: %d, " \
					"Write local full sync binlog context data file failed.",\
					__LINE__);
			return F_SYNC_ERROR;
		}
	}
	return fstate;
}

static enum full_sync_state __binlogmete_get_from_master(connect_info *cinfo,full_sync_binlog_mark *fmark)
{
	int ret;
	int64_t resp_bytes = 0;
	protocol_header *req_header;
	char req_buff[sizeof(protocol_header)];
	char resp_buff[LFS_STRUCT_PROP_LEN_SIZE4 + \
		LFS_STRUCT_PROP_LEN_SIZE8 * 2 + 1] = {0};

	memset(req_buff,0,sizeof(req_header));
	req_header = (protocol_header*)req_buff;
	req_header->header_s.body_len = 0;
	req_header->header_s.cmd = PROTOCOL_CMD_FULL_SYNC_GET_MASTER_BINLOG_METE;

	if((ret = client_senddata(cinfo->sfd,req_buff,sizeof(req_buff))) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"Send failed to master server(%s:%d) "\
					"get count of binlog files,errno:%d,"\
					"error info:%s!",\
				   	__LINE__,\
					cinfo->ipaddr,\
					cinfo->port,\
					errno,strerror(errno));
		return F_SYNC_NETWORK_ERROR;
	}
	if((ret = client_recvheader(cinfo->sfd,&resp_bytes)) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Failed to get the binlog information message "\
				"from master server(%s:%d),errno:%d," \
					"error info:%s!",\
				__LINE__,\
				cinfo->ipaddr,\
				cinfo->port,\
				errno,strerror(errno));
		return F_SYNC_NETWORK_ERROR;
	}
	if(resp_bytes == 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Have not received any binlog file count of "\
				"message from master server(%s:%d).",\
				__LINE__,\
				cinfo->ipaddr,\
				cinfo->port);
		return F_SYNC_ERROR;
	}
	if((ret = client_recvdata_nomalloc(cinfo->sfd,(char*)resp_buff,(const int64_t)resp_bytes)) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Failed to get the binlog information message data "\
				"from master server(%s:%d),errno:%d," \
					"error info:%s!",\
				__LINE__,\
				cinfo->ipaddr,\
				cinfo->port,\
				errno,strerror(errno));
		return F_SYNC_NETWORK_ERROR;
	}
	fmark->b_file_count = buff2int(resp_buff);
	fmark->rb_file_count = buff2int(resp_buff + LFS_STRUCT_PROP_LEN_SIZE4);
	return __full_sync_binlog_mark_write(fmark);
}

static enum full_sync_state __sync_local_binlog_data_from_master(connect_info *cinfo,full_sync_binlog_mark *fmark)
{
	int ret;
	enum full_sync_state fstate;
	protocol_header *req_header;
	int64_t body_len;
	int64_t b_filesize;
	int64_t resp_buff_len;
	char req_buff[sizeof(protocol_header) + LFS_STRUCT_PROP_LEN_SIZE4 + LFS_STRUCT_PROP_LEN_SIZE8];
	char resp_buff[LFS_STRUCT_PROP_LEN_SIZE8] = {0};
	char b_fn[BINLOG_FILE_NAME_SIZE];
	char *p;

	memset(req_buff,0,sizeof(req_buff));
	do
	{
		body_len = LFS_STRUCT_PROP_LEN_SIZE4 * 2 + LFS_STRUCT_PROP_LEN_SIZE8;
		req_header = (protocol_header*)req_buff;
		req_header->header_s.body_len = body_len;
		req_header->header_s.cmd = PROTOCOL_CMD_FULL_SYNC_COPY_MASTER_BINLOG; 
		p = req_buff + sizeof(protocol_header);
		int2buff(fmark->b_curr_sync_index,p);
		p += LFS_STRUCT_PROP_LEN_SIZE4;
		int2buff((int)LOCAL_BINLOG,p);
		p += LFS_STRUCT_PROP_LEN_SIZE4;
		long2buff((int64_t)fmark->b_offset,p);
		p += LFS_STRUCT_PROP_LEN_SIZE8;
		BINLOG_FILENAME(rbctx.binlog_file_name,fmark->b_curr_sync_index,b_fn)
		if((ret = senddata_nblock(cinfo->sfd,(void*)req_buff,(p - req_buff),confitems.network_timeout)) != 0)
		{
			logger_error("file: "__FILE__", line: %d," \
					"Send sync local binlog file %s request to master server(%s:%d) failed,errno:%d," \
					"error info:%s!",\
				   	__LINE__,\
					b_fn,\
					cinfo->ipaddr,\
					cinfo->port,\
					errno,strerror(errno));
			return F_SYNC_NETWORK_ERROR;
		}
		if((ret = client_recvheader(cinfo->sfd,&resp_buff_len)) != 0)
		{
			logger_error("file: "__FILE__", line: %d," \
					"Receive sync local binlog file %s response header packages from master server(%s:%d) failed.",\
				   	__LINE__,\
					b_fn,\
					cinfo->ipaddr,\
					cinfo->port);
			return F_SYNC_NETWORK_ERROR;
		}
		if((ret = client_recvdata_nomalloc(cinfo->sfd,resp_buff,resp_buff_len)) != 0)
		{
			logger_error("file: "__FILE__", line: %d," \
					"Receive sync local binlog file %s response packages from master server(%s:%d) failed.",\
				   	__LINE__,\
					b_fn,\
					cinfo->ipaddr,\
					cinfo->port);
			return F_SYNC_NETWORK_ERROR;
		}
		b_filesize = buff2long(resp_buff);
		if((fstate = __full_sync_append_remote_binlog(cinfo,fmark,(const int64_t)b_filesize)) != F_SYNC_OK)
		{
			__full_sync_binlog_mark_write(fmark);
			return fstate;
		}
	}while(0);
	fmark->b_curr_sync_index++;
	fmark->b_offset = 0;
	return __full_sync_binlog_mark_write(fmark);
}

static enum full_sync_state __sync_remote_binlog_data_from_master(connect_info *cinfo,full_sync_binlog_mark *fmark)
{
	int ret;
	protocol_header *req_header;
	int64_t body_len;
	int64_t b_filesize;
	int64_t resp_buff_len;
	char req_buff[sizeof(protocol_header) + LFS_STRUCT_PROP_LEN_SIZE4 + LFS_STRUCT_PROP_LEN_SIZE8];
	char resp_buff[LFS_STRUCT_PROP_LEN_SIZE8] = {0};
	char b_fn[BINLOG_FILE_NAME_SIZE];
	char *p;

	memset(req_buff,0,sizeof(req_buff));
	do
	{
		body_len = LFS_STRUCT_PROP_LEN_SIZE4 * 2 + LFS_STRUCT_PROP_LEN_SIZE8;
		req_header = (protocol_header*)req_buff;
		req_header->header_s.body_len = body_len;
		req_header->header_s.cmd = PROTOCOL_CMD_FULL_SYNC_COPY_MASTER_BINLOG; 
		p = req_buff + sizeof(protocol_header);
		int2buff(fmark->rb_curr_sync_index,p);
		p += LFS_STRUCT_PROP_LEN_SIZE4;
		int2buff((int)REMOTE_BINLOG,p);
		p += LFS_STRUCT_PROP_LEN_SIZE4;
		long2buff((int64_t)0,p);
		p += LFS_STRUCT_PROP_LEN_SIZE8;
		BINLOG_FILENAME(rbctx.binlog_file_name,fmark->rb_curr_sync_index,b_fn)
		if((ret = senddata_nblock(cinfo->sfd,(void*)req_buff,(p - req_buff),confitems.network_timeout)) != 0)
		{
			logger_error("file: "__FILE__", line: %d," \
					"Send sync remote binlog file %s request to master server(%s:%d) failed,errno:%d," \
					"error info:%s!",\
				   	__LINE__,\
					b_fn,\
					cinfo->ipaddr,\
					cinfo->port,\
					errno,strerror(errno));
			return F_SYNC_NETWORK_ERROR;
		}
		if((ret = client_recvheader(cinfo->sfd,&resp_buff_len)) != 0)
		{
			logger_error("file: "__FILE__", line: %d," \
					"Receive sync remote binlog file %s response header packages from master server(%s:%d) failed.",\
				   	__LINE__,\
					b_fn,\
					cinfo->ipaddr,\
					cinfo->port);
			return F_SYNC_NETWORK_ERROR;
		}
		if((ret = client_recvdata_nomalloc(cinfo->sfd,resp_buff,resp_buff_len)) != 0)
		{
			logger_error("file: "__FILE__", line: %d," \
					"Receive sync remote binlog file %s response packages from master server(%s:%d) failed.",\
				   	__LINE__,\
					b_fn,\
					cinfo->ipaddr,\
					cinfo->port);
			return F_SYNC_NETWORK_ERROR;
		}
		b_filesize = buff2long(resp_buff);
		if((ret = client_recvfile(cinfo->sfd,(const char*)b_fn,\
				0,(const int64_t)b_filesize,confitems.network_timeout)) != 0)
		{
			logger_error("file: "__FILE__", line: %d," \
					"Reveive %s remote binlog data from  master server(%s:%d) failed,errno:%d," \
					"error info:%s!",\
				   	__LINE__,\
					b_fn,\
					cinfo->ipaddr,\
					cinfo->port,\
					errno,strerror(errno));
			return F_SYNC_NETWORK_ERROR;
		}
	}while(0);
	fmark->rb_curr_sync_index++;
	return __full_sync_binlog_mark_write(fmark);
}

static enum full_sync_state __full_sync_append_remote_binlog(connect_info *cinfo,full_sync_binlog_mark *fmark,const int64_t bfile_size)
{
	enum full_sync_state rstate = F_SYNC_OK;
	int ret;
	int64_t remain_bytes;
	int recv_bytes;
	int in_bytes;

	in_bytes = 0;
	remain_bytes = bfile_size;
	binlog_ctx_lock();
	binlog_flush(&rbctx);
	while(remain_bytes > 0)
	{
		if(remain_bytes > BINLOG_CACHE_BUFFER_SIZE)
		{
			recv_bytes = BINLOG_CACHE_BUFFER_SIZE;
		}
		else
		{
			recv_bytes = remain_bytes;
		}
		if((ret = recvdata_nblock(cinfo->sfd,(void*)rbctx.binlog_wcache_buff,recv_bytes,confitems.network_timeout,&in_bytes)) != 0)
		{
			logger_error("file: "__FILE__", line: %d, " \
					"Receive master server(%s:%d) local binlog failed,"\
					" errno:%d,error info: %s.",\
				   	__LINE__,cinfo->ipaddr,cinfo->port,errno,strerror(errno));
			break;
			rstate = F_SYNC_NETWORK_ERROR;
		}
		rbctx.binlog_wcache_buff_len = in_bytes;
		if(binlog_flush(&rbctx) != 0)
		{
			logger_error("file: "__FILE__", line: %d, " \
					"Flush master server(%s:%d) local binlog to local failed,"\
					" errno:%d,error info: %s.",\
				   	__LINE__,cinfo->ipaddr,cinfo->port,errno,strerror(errno));
			break;
			rstate = F_SYNC_ERROR;
		}
		else
		{
			fmark->b_offset += in_bytes;
		}
		remain_bytes -= in_bytes;
	}
	binlog_ctx_unlock();
	return rstate;
}

static enum full_sync_state __full_sync_handle(connect_info *cinfo,sync_ctx *sctx,binlog_record *brecord)
{
	int ret;
	char req_buff[sizeof(protocol_header) + sizeof(sync_file_req)];
	char resp_buff[LFS_STRUCT_PROP_LEN_SIZE8] = {0};
	protocol_header *req_header;
	sync_file_req *sreq;
	int64_t resp_buff_len;
	int64_t file_size = 0;

	memset(req_buff,0,sizeof(req_buff));
	req_header = (protocol_header*)req_buff;
	req_header->header_s.body_len = sizeof(sync_file_req);
	req_header->header_s.cmd = PROTOCOL_CMD_FULL_SYNC_COPY_MASTER_DATA; 
	do
	{
		sreq = (sync_file_req*)(req_buff + sizeof(protocol_header));
		int2buff(strlen(brecord->f_block_map_name),sreq->sync_file_name_len);
		memcpy(sreq->sync_file_name,brecord->f_block_map_name,strlen(brecord->f_block_map_name));
		
		if((ret = senddata_nblock(cinfo->sfd,(void*)req_buff,sizeof(req_buff),confitems.network_timeout)) != 0)
		{
			logger_error("file: "__FILE__", line: %d," \
					"Send file id %s sync data request to master server(%s:%d) failed,errno:%d," \
					"error info:%s!",\
				   	__LINE__,\
					brecord->f_id,\
					cinfo->ipaddr,\
					cinfo->port,\
					errno,strerror(errno));
			return F_SYNC_NETWORK_ERROR;
		}
		if((ret = client_recvheader(cinfo->sfd,&resp_buff_len)) != 0)
		{
			logger_error("file: "__FILE__", line: %d," \
					"Receive file id %s sync data response header from master server(%s:%d) failed.",\
				   	__LINE__,\
					brecord->f_id,\
					cinfo->ipaddr,\
					cinfo->port);
			return F_SYNC_NETWORK_ERROR;
		}
		if((ret = client_recvdata_nomalloc(cinfo->sfd,resp_buff,resp_buff_len)) != 0)
		{
			logger_error("file: "__FILE__", line: %d," \
					"Receive file id %s response package from master server(%s:%d) failed.",\
				   	__LINE__,\
					brecord->f_id,\
					cinfo->ipaddr,\
					cinfo->port);
			return F_SYNC_NETWORK_ERROR;
		}
		file_size = buff2long(resp_buff);
		if((ret = client_recvfile(cinfo->sfd,(const char*)brecord->f_block_map_name,\
				0,(const int64_t)file_size,confitems.network_timeout)) != 0)
		{
			logger_error("file: "__FILE__", line: %d," \
					"Reveive file id %s sync data from  master server(%s:%d) failed,errno:%d," \
					"error info:%s!",\
				   	__LINE__,\
					brecord->f_id,\
					cinfo->ipaddr,\
					cinfo->port,\
					errno,strerror(errno));
			return F_SYNC_NETWORK_ERROR;
		}
	}while(0);
	return F_SYNC_OK;
}

