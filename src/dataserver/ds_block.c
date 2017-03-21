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
#include <unistd.h>
#include <string.h>
#include <sys/statvfs.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
#include <pthread.h>

#include "logger.h"
#include "shared_func.h"
#include "ds_func.h"
#include "ds_block.h"

#define DEFAULT_DATA_BLOCK_SUB_DIR_COUNT 256
#define FLUSH_BLOCK_STAT_DATA_FREQ 5000
#define MOUNT_BLOCK_STAT_DATA_FILE_NAME "block_snapshot.bat"
#define MOUNT_BLOCK_STAT_BINLOG_DATAFILEDS 5

block_mount mounts;
static int is_flush = 0;
static int is_statistics = 0;
static pthread_mutex_t mount_block_lock;


static int __mount_block_statvfs(void);
static int __mount_block_info_init(void);
static int __mount_block_stat_write(void);
static int __block_statvfs_cal(int mnt_index);
static int __block_reserve_check(int mnt_index);
static int __conn_mount_polling(conn *c);
static int __block_map_path_polling(file_ctx *fctx);


void MOUNT_BLOCK_LOCK()
{
	pthread_mutex_lock(&mount_block_lock);
}

void MOUNT_BLOCK_UNLOCK()
{
	pthread_mutex_unlock(&mount_block_lock);
}

int block_stat_flush()
{
	int ret;
	if(is_flush)
		return 0;
	is_flush = 1;
	if((ret = __mount_block_stat_write()) == 0)
	{
	}
	is_flush = 0;
	return ret;
}

int block_vfs_statistics(void)
{
	int i;
	if(is_statistics)
		return 0;
	is_statistics = 1;
	for(i = 0; i < mounts.mount_count; i++)
	{
		mounts.block_total_size_mb += mounts.blocks[i]->total_size_mb;
		mounts.block_free_size_mb += mounts.blocks[i]->free_size_mb;
	}
	is_statistics = 0;
	return 0;
}

int mount_block_map_init(void)
{
	int i,ret;
	const char sep = ':';
	int scount = 0;
	char **items = NULL;
	struct block_st *block = NULL;
	char fpath[MAX_PATH_SIZE];

	snprintf(fpath,sizeof(fpath),"%s/bin",base_path);
	if(!fileExists(fpath))
	{
		if(mkdir(fpath,0755) != 0)
		{
			logger_error("file: "__FILE__", line: %d," \
					"Mkdir \"%s\",errno:%d," \
					"error info:%s!", __LINE__,fpath,errno,strerror(errno));
			return errno;
		}
	}
	scount = get_split_str_count(confitems.block_mount,sep);
	items = (char**)malloc(sizeof(char*) * scount);
	if(items == NULL)
	{
		logger_error("file: "__FILE__", line: %d," \
				"Allocate memory to mount block point failed,errno:%d," \
				"error info:%s!", __LINE__,errno,strerror(errno));
		ret = errno;
		goto err;
	}
	memset(items,0,sizeof(char*) * scount);
	int count;
	if((count = splitStr(confitems.block_mount,sep,items,scount))\
			 != scount)
	{
		logger_error("file: "__FILE__", line: %d," \
				"Seperator block mount: %s failed." \
				, __LINE__,confitems.block_mount,errno,strerror(errno));
		ret = errno;
		goto err;
	}
	mounts.mount_count = scount;
	mounts.blocks = (struct block_st**)malloc(sizeof(struct block_st*) * mounts.mount_count);
	if(mounts.blocks == NULL)
	{
		logger_error("file: "__FILE__", line: %d," \
				"Allocate memory to mounts's blocks failed,errno:%d," \
				"error info:%s!", __LINE__,errno,strerror(errno));
		ret = errno;
		goto err;
	}
	memset(mounts.blocks,0,sizeof(struct block_st*) * mounts.mount_count);
	for(i = 0; i < scount; i++)
	{
		if(items[i] != NULL)
		{
			chopPath(items[i]);
			if(!fileExists(items[i]))
			{
				logger_error("file: "__FILE__", line: %d," \
						"\"%s\" is not mount point,can't be accessed,errno:%d," \
						"error info:%s!", __LINE__,items[i],errno,strerror(errno));
				ret = errno;
				goto err;
			}
			if(!isDir(items[i]))
			{
				logger_error("file: "__FILE__", line: %d," \
						"\"%s\" is not a directory,errno:%d," \
						"error info:%s!", __LINE__,items[i],errno,strerror(errno));
				ret = errno;
				goto err;
			}
			if((ret = block_dir_map(items[i])) != 0)
			{
				goto err;
			}
			block = (struct block_st*)malloc(sizeof(struct block_st));
			if(block == NULL)
			{
				logger_error("file: "__FILE__", line: %d," \
						"Allocate memory to \"%s\" mount block failed ,errno:%d," \
						"error info:%s!", __LINE__,items[i],errno,strerror(errno));
				ret = errno;
				goto err;
			}
			memset(block,0,sizeof(struct block_st));
			memcpy(block->mount_path,items[i],strlen(items[i]));
			block->total_size_mb = 0;
			block->free_size_mb = 0;
			mounts.blocks[i] = block;
		}
	}
	if((ret = pthread_mutex_init(&mount_block_lock,NULL)) != 0)
	{
		logger_error("file: "__FILE__", line: %d," \
				"Init mount block lock failed,errno:%d," \
				"error info:%s!", __LINE__,ret,strerror(ret));
		goto err;
	}
	if((ret = __mount_block_info_init()) != 0)
	{
		goto err;
	}
	return LFS_OK;
err:
	if(items)
	{
		free(items);
		items = NULL;
	}
	if(mounts.blocks)
	{
		free(mounts.blocks);
		mounts.blocks = NULL;
	}
	return ret;
}

void mount_block_map_destroy(void)
{
	if(mounts.blocks)
	{
		free(mounts.blocks);
		mounts.blocks = NULL;
	}
	pthread_mutex_destroy(&mount_block_lock);
}

int block_dir_map(const char *mount_path)
{
	char dpath[MAX_PATH_SIZE];
	char dname[8];
	char sname[8];
	char mispath[16];
	char maspath[16];
	int i,j;

	snprintf(dpath,sizeof(dpath),"%s",mount_path);
	if(!isDir(dpath))
	{
		if(mkdir(dpath,0755) != 0)
		{
			logger_error("file: "__FILE__", line: %d," \
					"Mkdir data block path\"%s\" failed,errno:%d," \
					"error info:%s!", __LINE__,dpath,errno,strerror(errno));
			return errno;
		}
	}
	if(chdir(dpath) != 0)
	{
			logger_error("file: "__FILE__", line: %d," \
					"Chdir \"%s\" failed,errno:%d," \
					"error info:%s!", __LINE__,dpath,errno,strerror(errno));
			return errno;
	}
	sprintf(mispath,"%02X/%02X",0,0);
	sprintf(maspath,"%02X/%02X",\
			confitems.block_sub_count - 1,\
			confitems.block_sub_count - 1);
	if(fileExists(mispath) && fileExists(maspath))
		return 0;
	logger_info("Started init mount point %s blocks map......",dpath);
	for(i = 0; i < confitems.block_sub_count;i++ )
	{
		sprintf(dname,"%02X",i);
		if(mkdir(dname,0755) != 0)
		{
			if(!(errno == EEXIST && isDir(dname)))
			{
				logger_error("file: "__FILE__", line: %d," \
						"Create block dir \"%s/%s\" failed,errno:%d," \
						"error info:%s!", __LINE__,dpath,dname,errno,strerror(errno));
				return errno;
			}
		}
		if(chdir(dname) != 0)
		{
			logger_error("file: "__FILE__", line: %d," \
					"Chdir \"%s/%s\" failed,errno:%d," \
					"error info:%s!", __LINE__,dpath,dname,errno,strerror(errno));
			return errno;
		}
		for(j = 0; j < confitems.block_sub_count; j++)
		{
			sprintf(sname,"%02X",j);
			if(mkdir(sname,0755) != 0)
			{
				if(!(errno == EEXIST && isDir(sname)))
				{
					logger_error("file: "__FILE__", line: %d," \
							"Create block dir \"%s/%s\" failed,errno:%d," \
							"error info:%s!", __LINE__,dpath,sname,errno,strerror(errno));
					return errno;
				}
			}
		}
		if(chdir("..") != 0)
		{
			logger_error("file: "__FILE__", line: %d," \
					"Chdir \"%s/%s\" failed,errno:%d," \
					"error info:%s!", __LINE__,dpath,sname,errno,strerror(errno));
			return errno;
		}
	}
	logger_info("Init mount point %s blocks map success.",dpath);
	return LFS_OK;
}

int mount_block_map_polling(conn *c)
{
	assert((c != NULL) && (c->fctx != NULL));
	int ret = LFS_OK;
	
	if((ret = __conn_mount_polling(c)) != LFS_OK)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Get current mount block index info failed.",\
				__LINE__);
		return ret;
	}
	if((ret = __block_map_path_polling(c->fctx)) != LFS_OK)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Get current block path map index info failed!",\
			   	__LINE__);
		return ret;
	}
	return ret;
}

static int __mount_block_statvfs(void)
{
	int i;
	struct statvfs sbuff;

	for(i = 0; i < mounts.mount_count; i++)
	{
		mounts.blocks[i]->pre_path_map = 0;
		mounts.blocks[i]->suf_path_map = 0;
		mounts.blocks[i]->curr_path_map_file_count = 0;
		if(statvfs(mounts.blocks[i]->mount_path,&sbuff) != 0)
		{
			logger_error("file: "__FILE__", line: %d, " \
					"Stat mount path \"%s\" statfs failed,errno:%d," \
					"error info:%s!", __LINE__,mounts.blocks[i]->mount_path,errno,strerror(errno));
			return EACCES;
		}
		mounts.blocks[i]->total_size_mb = ((int64_t)(sbuff.f_blocks) * \
				sbuff.f_frsize) / (1024 * 1024);
		mounts.blocks[i]->free_size_mb = ((int64_t)(sbuff.f_bavail) * \
				sbuff.f_frsize) / (1024 * 1024);
		mounts.block_total_size_mb += mounts.blocks[i]->total_size_mb;
		mounts.block_free_size_mb += mounts.blocks[i]->free_size_mb;
	}
	return LFS_OK;
}

static int __block_statvfs_cal(int mnt_index)
{
	struct statvfs sbuff;

	if(statvfs(mounts.blocks[mnt_index]->mount_path,&sbuff) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Stat mount path \"%s\" statfs failed,errno:%d," \
				"error info:%s!", __LINE__,mounts.blocks[mnt_index]->mount_path,errno,strerror(errno));
		return EACCES;
	}
	mounts.blocks[mnt_index]->total_size_mb = ((int64_t)(sbuff.f_blocks) * \
			sbuff.f_frsize) / (1024 * 1024);
	mounts.blocks[mnt_index]->free_size_mb = ((int64_t)(sbuff.f_bavail) * \
			sbuff.f_frsize) / (1024 * 1024);
	return LFS_OK;
}

static int __block_reserve_check(int mnt_index)
{
	if(!__block_statvfs_cal(mnt_index))
	{
		if(((float)((float)mounts.blocks[mnt_index]->free_size_mb / \
						(float)mounts.blocks[mnt_index]->total_size_mb)) < confitems.block_free_size_ratio)
		{
			return -1;
		}
	}
	return LFS_OK;
}

static int __mount_block_info_init(void)
{
	char fpath[MAX_PATH_SIZE];
	FILE *fp;
	char sline[256];
	char *fields[MOUNT_BLOCK_STAT_BINLOG_DATAFILEDS];
	int i,col_count;
	int ret;

	snprintf(fpath,sizeof(fpath),"%s/bin/%s",\
			base_path,MOUNT_BLOCK_STAT_DATA_FILE_NAME);
	if(!fileExists(fpath))
	{
		if((ret = __mount_block_statvfs()) != 0)
		{
			logger_error("file: "__FILE__", line: %d," \
					"Init mount block statvfs info.", __LINE__);
		}
		return ret;
	}
	if((fp = fopen(fpath,"r")) == NULL)	
	{
		logger_error("file: "__FILE__", line: %d," \
				"Open mount block stat info file \"%s\",errno:%d," \
				"error info:%s!", __LINE__,fpath,errno,strerror(errno));
		return errno;
	}
	i = 0;
	while(fgets(sline,sizeof(sline),fp) != NULL)
	{
		if(*sline == '\0')
		{
			continue;
		}
		col_count = splitStr(sline,BAT_DATA_SEPERATOR_SPLITSYMBOL,\
				fields,MOUNT_BLOCK_STAT_BINLOG_DATAFILEDS);
		if(col_count != MOUNT_BLOCK_STAT_BINLOG_DATAFILEDS)
		{
			logger_error("file: "__FILE__", line: %d," \
					"The snapshot of the mount blocks file  \"%s\" is invalid!",\
				   	__LINE__,fpath,fpath);
			break;
		}
		mounts.blocks[i]->pre_path_map = atoi(trim(fields[0])); 
		mounts.blocks[i]->suf_path_map = atoi(trim(fields[1]));
		mounts.blocks[i]->curr_path_map_file_count = \
													 atoi(trim(fields[2]));
		mounts.blocks[i]->total_size_mb = atoi(trim(fields[3]));
		mounts.blocks[i]->free_size_mb = atoi(trim(fields[4]));
		i++;
	}
	fclose(fp);
	return LFS_OK;
}

static int __mount_block_stat_write(void)
{
	char fname[MAX_PATH_SIZE];
	char buff[256];
	int fd,i,len;

	snprintf(fname,sizeof(fname),"%s/bin/%s",\
			base_path,MOUNT_BLOCK_STAT_DATA_FILE_NAME);
	if((fd = open(fname,O_WRONLY|O_CREAT|O_TRUNC,0644)) < 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Open mount blocks stat file \"%s\" failed,errno:%d," \
				"error info:%s!", __LINE__,fname,errno,strerror(errno));
		return errno;	
	}
	for(i = 0; i < mounts.mount_count; i++)
	{
		memset(buff,0,sizeof(buff));
		len = sprintf(buff,\
				"%d%c%d%c%d%c%d%c%d\n",\
				mounts.blocks[i]->pre_path_map,\
				BAT_DATA_SEPERATOR_SPLITSYMBOL,\
				mounts.blocks[i]->suf_path_map,\
				BAT_DATA_SEPERATOR_SPLITSYMBOL,\
				mounts.blocks[i]->curr_path_map_file_count,\
				BAT_DATA_SEPERATOR_SPLITSYMBOL,\
				mounts.blocks[i]->total_size_mb,\
				BAT_DATA_SEPERATOR_SPLITSYMBOL,
				mounts.blocks[i]->free_size_mb);
		if(write(fd,buff,len) != len)
		{
			logger_error("file: "__FILE__", line: %d, " \
					"flush mount path %s's block stat data to file  \"%s\" failed,errno:%d," \
					"error info:%s!",\
				   	__LINE__,mounts.blocks[i]->mount_path,\
					fname,errno,strerror(errno));
			close(fd);
			return errno;	
		}
	}
	close(fd);
	return LFS_OK;
}

static int __conn_mount_polling(conn *c)
{
	int mnt_index = c->sfd % mounts.mount_count;
	if(__block_reserve_check(mnt_index) != 0)
	{
		int i;
		for(i = 0; i < mounts.mount_count; i++)
		{
			if((i != mnt_index) && (__block_reserve_check(i) == 0))
			{
				c->fctx->f_mnt_block_index = i;
				break;
			}
		}
		if(i == mounts.mount_count - 1)
		{
			logger_crit("file: "__FILE__", line: %d," \
					"All mount block is no free space,please mount new block!", __LINE__);
			return LFS_ERROR;
		}
	}
	else
	{
		c->fctx->f_mnt_block_index = mnt_index;
	}
	return LFS_OK;
}

static int __block_map_path_polling(file_ctx *fctx)
{
	assert(fctx != NULL);
	int curr_file_count;

	curr_file_count = mounts.blocks[fctx->f_mnt_block_index]->\
					  curr_path_map_file_count;
	curr_file_count += 1;
	MOUNT_BLOCK_LOCK();
	if(curr_file_count >= \
			confitems.block_max_file_count)
	{
		mounts.blocks[fctx->f_mnt_block_index]->\
			curr_path_map_file_count = 0;
		mounts.blocks[fctx->f_mnt_block_index]->\
			suf_path_map += 1;
		if(mounts.blocks[fctx->f_mnt_block_index]->suf_path_map >= \
				confitems.block_sub_count)
		{
			mounts.blocks[fctx->f_mnt_block_index]->pre_path_map += 1;
			if(mounts.blocks[fctx->f_mnt_block_index]->pre_path_map >= \
					confitems.block_sub_count)
			{
				mounts.blocks[fctx->f_mnt_block_index]->pre_path_map = 0;
			}
			mounts.blocks[fctx->f_mnt_block_index]->suf_path_map = 0;
		}
		ctxs.block_opt_count += 1;
	}
	else
	{
		mounts.blocks[fctx->f_mnt_block_index]->curr_path_map_file_count = curr_file_count; 
	}
	MOUNT_BLOCK_UNLOCK();
	fctx->f_mp_pre = mounts.blocks[fctx->f_mnt_block_index]->pre_path_map;
	fctx->f_mp_suf = mounts.blocks[fctx->f_mnt_block_index]->suf_path_map;

	return LFS_OK;
}

