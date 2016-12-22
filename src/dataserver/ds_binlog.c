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
#include <time.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <assert.h>

#include "lfs_define.h"
#include "logger.h"
#include "ds_types.h"
#include "shared_func.h"
#include "ds_block.h"
#include "ds_binlog.h"


#define BINLOG_CACHE_BUFFER_SIZE (16 * 1024)
#define BINLOG_BUFFER_SIZE 64 * 1024
#define BINLOG_INDEX_FILE_NAME "binlog.index"

#define BINLOG_FILE_READ_END_FLAG 1
#define BINLOG_MAX_FILE_SIZE 1024 * 1024 * 1024
#define SYNC_BAT_DATAFIELDS 4
#define BINLOG_RECORD_COLUMN 3

#define SYNC_MARK_FILENAME(bid,fn_buff) \
	snprintf(fn_buff,MAX_PATH_SIZE,"%s/sync/%s.mark",\
			base_path,bid);

int binlog_fd = -1;
int curr_binlog_file_index = 0;
int64_t binlog_file_size = 0;
time_t binlog_file_update_timestamp;
static pthread_mutex_t binlog_thread_lock;
static char *binlog_wcache_buff = NULL;
static int binlog_wcache_buff_len = 0;

static int __write_curr_binlog_file_index(int findex);
static int __binlog_flush();
static int __binlog_lock_flush();
static enum binlog_file_state  __binlog_record_read(sync_ctx *sctx,char *record_buff,int *record_length);
static enum binlog_file_state __binlog_file_read(sync_ctx *sctx);
static enum binlog_file_state __open_new_binlog_file();
static int __mark_file_batdata_load(sync_ctx *sctx);
static int __timestamp_binlog_record_locate(const time_t timestamp,int *bindex,int64_t *boffset);
static int __open_setting_binlog(sync_ctx *sctx);
static enum binlog_file_state __binlog_record_parse(sync_ctx *sctx,char *record,binlog_record *brecord);
static enum binlog_file_state __binlog_record_read_do(sync_ctx *sctx,char *record_buff,int *record_length);

int binlog_init(void)
{
	int ret = LFS_OK;
	char fpath[MAX_PATH_SIZE];
	char ifname[MAX_PATH_SIZE];
	char bfname[MAX_PATH_SIZE];
	int inx_fd;
	int inx_rbytes;
	char fbuff[64];

	snprintf(fpath,sizeof(fpath),"%s/bin",base_path);
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
	snprintf(ifname,sizeof(ifname),"%s/bin/%s",\
			base_path,BINLOG_INDEX_FILE_NAME);
	if((inx_fd = open(ifname,O_RDONLY)) >= 0)
	{
		inx_rbytes = read(inx_fd,fbuff,sizeof(fbuff) - 1);
		close(inx_fd);
		if(inx_rbytes <= 0)
		{
			logger_error("file: "__FILE__", line: %d," \
					"Read file  \"%s\" failed.", __LINE__,ifname);
			return LFS_ERROR;
		}
		fbuff[inx_rbytes] = '\0';
		curr_binlog_file_index = atoi(fbuff);
		if(curr_binlog_file_index < 0)
		{
			logger_error("file: "__FILE__", line: %d," \
					"In file  \"%s\",binlog file index %d is invalid.", __LINE__,ifname,curr_binlog_file_index);
			return LFS_ERROR;
		}
	}
	else
	{
		curr_binlog_file_index = 0;
		if((ret = __write_curr_binlog_file_index(curr_binlog_file_index)) != 0)
		{
			return ret;
		}

	}
	binlog_wcache_buff = (char*)malloc(BINLOG_CACHE_BUFFER_SIZE);
	if(binlog_wcache_buff == NULL)
	{
		logger_error("file: "__FILE__", line: %d," \
				"Allocate binlog write cache buffer memory failed,errno:%d,error info:%s.",\
			   	__LINE__,errno,strerror(errno));
		return LFS_ERROR;
	}
	snprintf(bfname,sizeof(bfname),"%s/bin/%s.%03d",\
			base_path,BINLOG_FILE_NAME,curr_binlog_file_index);
	if((binlog_fd = open(bfname,O_WRONLY | O_CREAT | O_APPEND,0644)) < 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Open \"%s\" failed,errno:%d," \
				"error info:%s!", __LINE__,bfname,errno,strerror(errno));
		ret = LFS_ERROR;
		goto err;
	}
	binlog_file_size = lseek(binlog_fd,0,SEEK_END);
	if(binlog_file_size < 0)
	{
		logger_error("file: "__FILE__", line: %d," \
				"lseek file \"%s\" failed,errno:%d," \
				"error info:%s!", __LINE__,bfname,errno,strerror(errno));
		ret = LFS_ERROR;
		goto err;
	}
	if((ret = pthread_mutex_init(&binlog_thread_lock,NULL)) != 0)
	{
		logger_error("file: "__FILE__", line: %d," \
				"Init binlog thread lock failed,errno:%d," \
				"error info:%s!", __LINE__,ret,strerror(ret));
		ret = LFS_ERROR;
		goto err;
	}
	return LFS_OK;
err:
	binlog_destroy();
	return ret;
}

int binlog_destroy(void)
{
	if(binlog_fd >= 0)
	{
		__binlog_lock_flush();
		close(binlog_fd);
		binlog_fd = -1;
	}
	if(binlog_wcache_buff != NULL)
	{
		free(binlog_wcache_buff);
		binlog_wcache_buff = NULL;
		pthread_mutex_destroy(&binlog_thread_lock);
	}
	return LFS_OK;
}

int binlog_write(const char *line)
{
	int ret = LFS_OK;
	if((ret = pthread_mutex_lock(&binlog_thread_lock)) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Call binlog lock failed,errno:%d," \
				"error info:%s!", __LINE__,ret,strerror(ret));
	}

	binlog_wcache_buff_len += sprintf(binlog_wcache_buff + \
			binlog_wcache_buff_len,"%s",line);

	if(BINLOG_CACHE_BUFFER_SIZE - binlog_wcache_buff_len < 256)
	{
		ret = __binlog_flush();
	}
#ifdef _DEBUG_
		ret = __binlog_flush();
#endif
	if((ret = pthread_mutex_unlock(&binlog_thread_lock)) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Call binlog unlock failed,errno:%d," \
				"error info:%s!", __LINE__,ret,strerror(ret));
	}
	return ret;
}

int curr_binlog_file_index_set(int bindex)
{
	int ret;
	if((ret = pthread_mutex_lock(&binlog_thread_lock)) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Call binlog lock failed,errno:%d," \
				"error info:%s!", __LINE__,ret,strerror(ret));
		return ret;
	}
	curr_binlog_file_index = bindex;
	if((ret = pthread_mutex_unlock(&binlog_thread_lock)) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Call binlog unlock failed,errno:%d," \
				"error info:%s!", __LINE__,ret,strerror(ret));
		return ret;
	}
	return LFS_OK;
}

int curr_binlog_file_mete_get(binlog_file_mete *bfmete)
{
	assert(bfmete != NULL);
	int ret;
	if((ret = pthread_mutex_lock(&binlog_thread_lock)) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Call binlog lock failed,errno:%d," \
				"error info:%s!", __LINE__,ret,strerror(ret));
		return ret;
	}
	bfmete->cindex = curr_binlog_file_index;
	bfmete->coffset = binlog_file_size;
	bfmete->cupdtimestamp = binlog_file_update_timestamp;
	if((ret = pthread_mutex_unlock(&binlog_thread_lock)) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Call binlog unlock failed,errno:%d," \
				"error info:%s!", __LINE__,ret,strerror(ret));
		return ret;
	}
	return LFS_OK;
}

enum binlog_file_state binlog_read(sync_ctx *sctx,binlog_record *brecord,int *brecord_size)
{
	assert(sctx != NULL);
	assert(brecord != NULL);
	int ret;
	char record[BINLOG_RECORD_SIZE] = {0};
	char b_fn[BINLOG_FILE_NAME_SIZE] = {0};
	enum binlog_file_state bfs;

	memset(brecord,0,sizeof(binlog_record));
	while(1)
	{
		bfs = __binlog_record_read(sctx,record,brecord_size);
		if(bfs == B_FILE_OK)
		{
			break;
		}
		else if(bfs != B_FILE_END)
		{
			return bfs;
		}
		if(sctx->b_index > curr_binlog_file_index)
		{
			return B_FILE_ERROR;
		}
		BINLOG_FILENAME(sctx->b_index,b_fn)
		if(sctx->b_buff.length != 0)
		{
			logger_error("file: "__FILE__", line: %d, " \
					"Binlog file \"%s\" not to the tail by file offset %d.",\
					__LINE__,\
					b_fn,sctx->b_buff.length);
			return B_FILE_ERROR;
		}
		sctx->b_index++;
		sctx->b_offset = 0;
		sctx->b_buff.flag = 0;
		if((ret = __open_new_binlog_file()) != 0)
		{
			return ret;
		}
		if((ret = sync_mark_file_batdata_write(sctx)) != 0)
		{
			return B_FILE_ERROR;
		}
	}
	if((ret = __binlog_record_parse(sctx,record,brecord)))
	{
		return ret;
	}
	return B_FILE_OK;
}

int asyncctx_init(block_brief *bbrief,sync_ctx *sctx)
{
	assert(bbrief != NULL);
	assert(sctx != NULL);
	int ret;
	char m_fn[MAX_PATH_SIZE] = {0};

	memset(sctx,0,sizeof(sync_ctx));
	sctx->b_fd = -1;
	sctx->m_fd = -1;
	sctx->b_buff.buff = (char*)malloc(BINLOG_BUFFER_SIZE);	
	if(sctx->b_buff.buff == NULL)
	{
		logger_error("file: "__FILE__", line: %d," \
				"Allocate sync context buffer memory failed,errno:%d,error info:%s.",\
			   	__LINE__,errno,strerror(errno));
		return LFS_ERROR;
	}
	memset(sctx->b_buff.buff,0,BINLOG_BUFFER_SIZE);
	sctx->b_buff.cbuff = sctx->b_buff.buff;
	snprintf(sctx->f_mark,sizeof(sctx->f_mark),\
			"to_%s:%d",bbrief->ipaddr,bbrief->port);
	SYNC_MARK_FILENAME(sctx->f_mark,m_fn)
	if(fileExists(m_fn))
	{
		if((ret = __mark_file_batdata_load(sctx)) != 0)
		{
			logger_error("file: "__FILE__", line: %d, " \
					"Load mark file \"%s\" failed." \
					, __LINE__,m_fn);
			return ret;
		}
	}
	else
	{
		sctx->b_index = curr_binlog_file_index;
		if((ret = __timestamp_binlog_record_locate((const time_t)bbrief->last_synctimestamp,\
						&sctx->b_index,&sctx->b_offset)) != 0)
		{
			return ret;
		}
		if((ret = sync_mark_file_batdata_write(sctx)) != 0)
		{
			return ret;
		}
	}
	if((ret = __open_setting_binlog(sctx)) != 0)
	{
		return ret;
	}
	return LFS_OK;
}

int fullsyncctx_init(sync_ctx *sctx,connect_info *cinfo)
{
	assert(sctx != NULL);
	assert(cinfo != NULL);
	int ret;
	char m_fn[MAX_PATH_SIZE] = {0};

	memset(sctx,0,sizeof(sync_ctx));
	sctx->b_fd = -1;
	sctx->m_fd = -1;
	sctx->b_buff.buff = (char*)malloc(BINLOG_BUFFER_SIZE);	
	if(sctx->b_buff.buff == NULL)
	{
		logger_error("file: "__FILE__", line: %d," \
				"Allocate sync context buffer memory failed,errno:%d,error info:%s.",\
			   	__LINE__,errno,strerror(errno));
		return LFS_ERROR;
	}
	memset(sctx->b_buff.buff,0,BINLOG_BUFFER_SIZE);
	sctx->b_buff.cbuff = sctx->b_buff.buff;
	snprintf(sctx->f_mark,sizeof(sctx->f_mark),\
			"from_%s:%d",cinfo->ipaddr,cinfo->port);
	SYNC_MARK_FILENAME(sctx->f_mark,m_fn)
	if(fileExists(m_fn))
	{
		if((ret = __mark_file_batdata_load(sctx)) != 0)
		{
			logger_error("file: "__FILE__", line: %d, " \
					"Load mark file \"%s\"'s bat data failed." \
					, __LINE__,m_fn);
			return ret;
		}
	}
	if((ret = __open_setting_binlog(sctx)) != 0)
	{
		return ret;
	}
	return LFS_OK;
}

int sync_mark_file_batdata_write(sync_ctx *sctx)
{
	int fd;
	char buff[BINLOG_BAT_LINE_BUFF_SIZE] = {0};
	char m_fn[MAX_PATH_SIZE] = {0};
	int len,ret = LFS_OK;

	SYNC_MARK_FILENAME(sctx->f_mark,m_fn)
	if(sctx->m_fd < 0)
	{
		if((fd = open(m_fn,O_WRONLY | O_CREAT | O_TRUNC,0644)) < 0)
		{
			logger_error("file: "__FILE__", line: %d, " \
					"Open \"%s\" failed,errno:%d," \
					"error info:%s!", __LINE__,m_fn,errno,strerror(errno));
			return LFS_ERROR;
		}
	}
	len = sprintf(buff,\
			"%d%c%ld%c%ld%c%ld\n",\
			sctx->b_index,\
			BAT_DATA_SEPERATOR_SPLITSYMBOL,\
			sctx->last_timestamp,\
			BAT_DATA_SEPERATOR_SPLITSYMBOL,\
			sctx->b_offset,\
			BAT_DATA_SEPERATOR_SPLITSYMBOL,\
			sctx->sync_count);
	if(write(fd,buff,len) != len)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Write mark file \"%s\",s bat data failed,errno:%d," \
				"error info:%s!", __LINE__,m_fn,errno,strerror(errno));
		ret = (errno != 0)?errno:EIO;
	}
	sctx->last_sync_count = sctx->sync_count;
	return ret;
}

int binlogsyncctx_destroy(sync_ctx *sctx)
{
	assert(sctx != NULL);
	if(sctx->b_fd >= 0)
	{
		close(sctx->b_fd);
		sctx->b_fd = -1;
	}
	if(sctx->m_fd >= 0)
	{
		close(sctx->m_fd);
		sctx->m_fd = -1;
	}
	if(sctx->b_buff.buff != NULL)
	{
		free(sctx->b_buff.buff);
		sctx->b_buff.buff = NULL;
		sctx->b_buff.cbuff = NULL;
		sctx->b_buff.length = 0;
	}
	return LFS_OK;
}

static int __binlog_lock_flush()
{
	int ret;
	if((ret = pthread_mutex_lock(&binlog_thread_lock)) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Call binlog lock failed,errno:%d," \
				"error info:%s!", __LINE__,ret,strerror(ret));
	}
	__binlog_flush();
	if((ret = pthread_mutex_unlock(&binlog_thread_lock)) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Call binlog unlock failed,errno:%d," \
				"error info:%s!", __LINE__,ret,strerror(ret));
	}
	return LFS_OK;
}

static int __binlog_flush()
{
	int ret = LFS_OK;

	if(binlog_wcache_buff_len == 0)
	{
		return LFS_OK;
	}
	else if(write(binlog_fd,binlog_wcache_buff,\
				binlog_wcache_buff_len) != \
			binlog_wcache_buff_len)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Write binlog to file  \"%s/%s.%03d\" failed,errno:%d," \
				"error info:%s!",\
			   	__LINE__,\
				base_path,\
				BINLOG_FILE_NAME,\
				curr_binlog_file_index,\
				errno,\
				strerror(errno));
		return LFS_ERROR;
	}
	else if(fsync(binlog_fd) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Sync binlog to file  \"%s/%s.%03d\" failed,errno:%d," \
				"error info:%s!",\
			   	__LINE__,\
				base_path,\
				BINLOG_FILE_NAME,\
				curr_binlog_file_index,\
				errno,\
				strerror(errno));
		return LFS_ERROR;
	}
	else
	{
		binlog_file_size += binlog_wcache_buff_len;
		binlog_file_update_timestamp = time(NULL);
		if(binlog_file_size >= BINLOG_MAX_FILE_SIZE)
		{
			if((ret = __write_curr_binlog_file_index(curr_binlog_file_index + 1)) == 0)
			{
				curr_binlog_file_index += 1;
				ret = __open_new_binlog_file();
				if(ret != 0)
				{
					logger_error("file: "__FILE__", line: %d, " \
							"Open binlog file  \"%s/%s.%03d\" failed,errno:%d," \
							"error info:%s!",\
							__LINE__,\
							base_path,\
							BINLOG_FILE_NAME,\
							(curr_binlog_file_index + 1),\
							errno,\
							strerror(errno));
				}
			}
			binlog_file_size = 0;
		}
		memset(binlog_wcache_buff,0,BINLOG_CACHE_BUFFER_SIZE);
		binlog_wcache_buff_len = 0;
	}
	return ret;
}

static int __write_curr_binlog_file_index(int findex)
{
	char fname[MAX_PATH_SIZE];
	char buff[32];
	int fd;
	int len;

	snprintf(fname,sizeof(fname),"%s/bin/%s",\
			base_path,BINLOG_INDEX_FILE_NAME);
	if((fd = open(fname,O_WRONLY|O_CREAT|O_TRUNC,0644)) < 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Open \"%s\" failed,errno:%d," \
				"error info:%s!", __LINE__,fname,errno,strerror(errno));
		return LFS_ERROR;	
	}
	len = sprintf(buff,"%d",findex);
	if(write(fd,buff,len) != len)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Write binlog file index %d to file  \"%s\" failed,errno:%d," \
				"error info:%s!", __LINE__,findex,fname,errno,strerror(errno));
		close(fd);
		return LFS_ERROR;	
	}
	close(fd);
	return LFS_OK;
}

static enum binlog_file_state __open_new_binlog_file()
{
	char fname[MAX_PATH_SIZE];

	if(binlog_fd >= 0)
	{
		close(binlog_fd);
		binlog_fd = -1;
	}
	snprintf(fname,sizeof(fname),"%s/bin/%s.%03d",\
			base_path,BINLOG_FILE_NAME,curr_binlog_file_index);
	if(fileExists(fname))
	{
		logger_warning("file: "__FILE__", line: %d, " \
				"Binlog file \"%s\" is exists and truncate!",\
			   	__LINE__,fname);
	}
	binlog_fd = open(fname,O_WRONLY|O_CREAT|O_APPEND,0644);
	if(binlog_fd < 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Open \"%s\" failed,errno:%d," \
				"error info:%s!", __LINE__,fname,errno,strerror(errno));
		return B_FILE_ERROR;
	}
	return B_FILE_OK;
}

static int __mark_file_batdata_load(sync_ctx *sctx)
{
	assert(sctx != NULL);
	int ret = LFS_OK;
	FILE *fp;
	char sline[BINLOG_RECORD_SIZE] = {0};
	char *fields[SYNC_BAT_DATAFIELDS];
	char m_fn[MAX_PATH_SIZE] = {0};
	int col_count;

	SYNC_MARK_FILENAME(sctx->f_mark,m_fn)
	if((fp = fopen(m_fn,"r")) == NULL)	
	{
		logger_error("file: "__FILE__", line: %d," \
				"Open file \"%s\",errno:%d," \
				"error info:%s!", __LINE__,m_fn,errno,strerror(errno));
		return ret;
	}
	while(fgets(sline,sizeof(sline),fp) != NULL)
	{
		if(*sline == '\0')
		{
			continue;
		}
		col_count = splitStr(sline,BAT_DATA_SEPERATOR_SPLITSYMBOL,\
				fields,SYNC_BAT_DATAFIELDS);
		if(col_count != SYNC_BAT_DATAFIELDS)
		{
			logger_error("file: "__FILE__", line: %d," \
					"The sync data \"%s\" is invalid!",\
				   	__LINE__,m_fn);
			break;
		}
		sctx->b_index = atoi(trim(fields[0]));
		sctx->last_timestamp = (time_t)atol(trim(fields[1]));
		sctx->b_offset = (int64_t)atol(trim(fields[2]));
		sctx->sync_count = (int64_t)atol(trim(fields[3]));
		sctx->last_sync_count = sctx->sync_count;
	}
	fclose(fp);
	return LFS_OK;
}

static int __timestamp_binlog_record_locate(const time_t timestamp,int *bindex,int64_t *boffset)
{
	char b_fn[BINLOG_FILE_NAME_SIZE];
	FILE *fp;
	char sline[BINLOG_RECORD_SIZE] = {0};
	char *fields[SYNC_BAT_DATAFIELDS];
	int col_count;
	time_t stimestamp = timestamp;
	time_t rtimestamp;
	int cindex = *bindex;
	int64_t offset = 0;


	BINLOG_FILENAME(cindex,b_fn)
	if((fp = fopen(b_fn,"r")) == NULL)	
	{
		logger_error("file: "__FILE__", line: %d," \
				"Open binlog file \"%s\",errno:%d," \
				"error info:%s!", __LINE__,b_fn,errno,strerror(errno));
		return errno;
	}

	while(fgets(sline,sizeof(sline),fp) != NULL)
	{
		if(*sline == '\0')
		{
			continue;
		}
		col_count = splitStr(sline,' ',\
				fields,BINLOG_RECORD_COLUMN);
		if(col_count != BINLOG_RECORD_COLUMN)
		{
			logger_error("file: "__FILE__", line: %d, " \
					"In binlog file \"%s\","\
					"record %s item count:%d < %d,errno:%d," \
					"error info:%s!",\
					__LINE__,\
					b_fn,\
					sline,\
					col_count,\
					BINLOG_RECORD_COLUMN,\
					errno,\
					strerror(errno));
			goto err;
		}
		rtimestamp = (time_t)atoi(fields[0]);
		if(rtimestamp > stimestamp)
		{
			break;
		}
		else
		{
			offset += strlen(sline);
		}
		memset(sline,0,sizeof(sline));
	}

	*boffset = offset;
	*bindex = cindex;
err:
	fclose(fp);
	return LFS_OK;
}

static int __open_setting_binlog(sync_ctx *sctx)
{
	assert(sctx != NULL);
	char b_fn[BINLOG_FILE_NAME_SIZE] = {0};

	BINLOG_FILENAME(sctx->b_index,b_fn)
	if(sctx->b_fd >= 0)
	{
		close(sctx->b_fd);
	}
	sctx->b_fd = open(b_fn,O_RDONLY);
	if(sctx->b_fd < 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Open binlog file  \"%s\" failed,errno:%d," \
				"error info:%s!",\
				__LINE__,\
				b_fn,\
				errno,\
				strerror(errno));
		return errno;
	}
	if(sctx->b_offset > 0)
	{
		if(lseek(sctx->b_fd,sctx->b_offset,SEEK_SET) < 0)
		{
			close(sctx->b_fd);
			sctx->b_fd = -1;
			logger_error("file: "__FILE__", line: %d, " \
					"Set binlog file  \"%s\" offset failed,errno:%d," \
					"error info:%s!",\
					__LINE__,\
					b_fn,\
					errno,\
					strerror(errno));
			return errno;
		}
	}
	return LFS_OK;
}

static enum binlog_file_state __binlog_record_parse(sync_ctx *sctx,char *record,binlog_record *brecord)
{
	assert(record != NULL);
	int ret;
	char b_fn[BINLOG_FILE_NAME_SIZE];
	char *cols[BINLOG_RECORD_COLUMN];

	BINLOG_FILENAME(sctx->b_index,b_fn)
	if((ret = splitStr(record,' ',cols,BINLOG_RECORD_COLUMN)) < BINLOG_RECORD_COLUMN)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"In binlog file \"%s\",file offset:%ld,"\
				"record %s item count:%d < %d,errno:%d," \
				"error info:%s!",\
			   	__LINE__,\
				b_fn,\
				sctx->b_offset,\
				record,\
				ret,\
				BINLOG_RECORD_COLUMN,\
				errno,\
				strerror(errno));
		return B_FILE_ERROR;
	}
	brecord->timestamp = (time_t)atoi(cols[0]);
	brecord->op_type = *(cols[1]);
	brecord->f_id_length = strlen(cols[2]) - 1; 
	if(brecord->f_id_length > sizeof(brecord->f_id) - 1)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"In binlog file \"%s\",file offset:%ld,"\
				"record %s's file id is invalid,"\
				"file id length:%d > %d,errno:%d," \
				"error info:%s!",\
			   	__LINE__,\
				b_fn,\
				sctx->b_offset,\
				record,\
				brecord->f_id_length,\
				(int)(sizeof(brecord->f_id) - 1),\
				errno,\
				strerror(errno));
		return B_FILE_ERROR;
	}
	memcpy(brecord->f_id,cols[2],brecord->f_id_length);
	LFS_SPLIT_BLOCK_INDEX_BY_FILE_ID(brecord->f_id)
	brecord->mnt_block_index = (int)atoi(pblock_index);			
	memcpy(brecord->f_map_name,fid_map_name_buff,strlen(fid_map_name_buff));
	sprintf(brecord->f_block_map_name,"%s/%s",mounts.blocks[brecord->mnt_block_index]->mount_path,brecord->f_map_name);
	return B_FILE_OK;
}

static enum binlog_file_state __binlog_file_read(sync_ctx *sctx)
{
	assert(sctx != NULL);
	int rbytes;
	char b_fn[BINLOG_FILE_NAME_SIZE];

	if((sctx->b_buff.flag == BINLOG_FILE_READ_END_FLAG) && \
			sctx->b_buff.length == 0)
	{
		return B_FILE_END;
	}
	if(sctx->b_buff.cbuff != sctx->b_buff.buff)
	{
		if(sctx->b_buff.length > 0)
		{
			memcpy(sctx->b_buff.buff,sctx->b_buff.cbuff,sctx->b_buff.length);
		}
		sctx->b_buff.cbuff = sctx->b_buff.buff;
	}	
	BINLOG_FILENAME(sctx->b_index,b_fn)
	rbytes = read(sctx->b_fd,sctx->b_buff.buff + sctx->b_buff.length,BINLOG_BUFFER_SIZE - sctx->b_buff.length);
	if(rbytes < 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Read binlog file \"%s\" failed,file offset:%ld,errno:%d," \
				"error info:%s!",\
			   	__LINE__,\
				b_fn,\
				sctx->b_offset + sctx->b_buff.length,\
				errno,\
				strerror(errno));
		return B_FILE_ERROR;
	}
	else if(rbytes == 0)
	{
		if(sctx->b_index == curr_binlog_file_index)
		{
			return B_FILE_NODATA;
		}
		sctx->b_buff.flag = BINLOG_FILE_READ_END_FLAG;
		return B_FILE_END;
	}
	sctx->b_buff.length += rbytes;
	return B_FILE_OK;
}

static enum binlog_file_state  __binlog_record_read(sync_ctx *sctx,char *record_buff,int *record_length)
{
	assert(sctx != NULL);
	enum binlog_file_state b_fs;
	b_fs = __binlog_record_read_do(sctx,record_buff,record_length);
	if(b_fs != B_FILE_NODATA)
	{
		return b_fs;
	}
	if((b_fs = __binlog_file_read(sctx)) != B_FILE_OK)
	{
		return b_fs;
	}
	return __binlog_record_read_do(sctx,record_buff,record_length);
}

static enum binlog_file_state  __binlog_record_read_do(sync_ctx *sctx,char *record_buff,int *record_length)
{
	char *ple = NULL;
	char b_fn[BINLOG_FILE_NAME_SIZE] = {0};

	BINLOG_FILENAME(sctx->b_index,b_fn)
	if(sctx->b_buff.length == 0)
	{
		*record_length = 0;
		return B_FILE_NODATA;
	}
	ple = (char*)(memchr(sctx->b_buff.cbuff,'\n',sctx->b_buff.length));
	if(ple == NULL)
	{
		*record_length = 0;
		return B_FILE_NODATA;
	}
	*record_length = (ple - sctx->b_buff.cbuff) + 1;
	if(*record_length >= BINLOG_RECORD_SIZE)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Read binlog file \"%s\" failed,file offset:%ld,"\
				"record buff size:%d <= record size:%d,errno:%d," \
				"error info:%s!",\
			   	__LINE__,\
				b_fn,\
				sctx->b_offset,\
				BINLOG_RECORD_SIZE,\
				*record_length,\
				errno,\
				strerror(errno));
		return B_FILE_ERROR;
	}
	memcpy(record_buff,sctx->b_buff.cbuff,(*record_length));
	*(record_buff + (*record_length)) = '\0';
	sctx->b_buff.cbuff = ple + 1;
	sctx->b_buff.length -= *record_length;
	return B_FILE_OK;
}

