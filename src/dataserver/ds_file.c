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
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <event.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/statvfs.h>
#include <sys/stat.h>

#include "logger.h"
#include "base64.h"
#include "crc32.h"
#include "shared_func.h"
#include "ds_block.h"
#include "ds_file.h"


file_ctx **fctxs = NULL;
static int __fileidname_pack(file_ctx *fctx);
static int __mapname_pack(file_ctx *fctx);
static int __blockmapname_pack(file_ctx *fctx);

int file_ctx_mpools_init(void)
{
	if((fctxs = calloc(ctxs.max_fds,sizeof(file_ctx*))) == NULL)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Failed to allocate file context pool!", __LINE__);
		return LFS_ERROR;
	}
	return LFS_OK;
}

void file_ctx_mpools_destroy(void)
{
	assert(fctxs != NULL);
	int i;
	for(i = 0; i < ctxs.max_fds; i++)
	{
		if(fctxs[i])
		{
			free(fctxs[i]);
			fctxs[i] = NULL;
		}
	}
	free(fctxs);
	fctxs = NULL;
}

file_ctx* file_ctx_new(const int sfd,const int buffer_size,enum file_op_type opt)
{
	file_ctx *fctx = NULL;
	assert(sfd >= 0 && sfd < ctxs.max_fds);
	fctx = fctxs[sfd];
	if(fctx == NULL)
	{
		fctx = (file_ctx*)malloc(sizeof(struct file_ctx_st));
		if(!fctx)
		{
			logger_error("file: "__FILE__", line: %d," \
					"Allocate memory to file context failed,errno:%d," \
					"error info:%s!", __LINE__,errno,strerror(errno));
			return NULL;
		}
		memset(fctx,0,sizeof(struct file_ctx_st));
		fctx->fd = -1;
		fctx->f_op_flags = 0;
		fctx->f_mnt_block_index = -1;
		fctx->f_mp_pre = -1;
		fctx->f_mp_suf = -1;
		fctx->f_op_type = opt;
		fctx->f_crc32 = 0;
		fctx->alloc_count = 0;
		fctx->f_buff_size = buffer_size;
		fctx->f_roffset = fctx->f_woffset = fctx->f_buff_offset = 0;
		fctx->f_size = fctx->f_offset = fctx->f_total_offset = fctx->f_total_size = 0;
		fctx->f_op_func = NULL;
		fctx->f_dio_func = NULL;
		fctx->f_cleanup_func = NULL;
		fctx->f_buff = (char*)malloc(buffer_size);
		memset(fctx->f_orginl_name,0,sizeof(fctx->f_orginl_name));
		memset(fctx->f_b64_name,0,sizeof(fctx->f_b64_name));
		memset(fctx->f_map_name,0,sizeof(fctx->f_map_name));
		memset(fctx->f_block_map_name,0,sizeof(fctx->f_block_map_name));
		memset(fctx->f_id,0,sizeof(fctx->f_id));
		memset(fctx->f_path_name,0,sizeof(fctx->f_path_name));
		if(fctx->f_buff == NULL)
		{
			file_ctx_free(fctx);
			return NULL;
		}
		fctxs[sfd] = fctx;
	}
	fctx->fd = -1;
	fctx->f_op_flags = 0;
	fctx->f_mnt_block_index = -1;
	fctx->f_mp_pre = -1;
	fctx->f_mp_suf = -1;
	fctx->f_op_type = opt;
	fctx->f_crc32 = 0;
	fctx->alloc_count = 0;
	fctx->f_roffset = fctx->f_woffset = fctx->f_buff_offset = 0;
	fctx->f_size = fctx->f_offset = fctx->f_total_offset = fctx->f_total_size = 0;
	fctx->f_op_func = NULL;
	fctx->f_dio_func = NULL;
	fctx->f_cleanup_func = NULL;
	memset(fctx->f_orginl_name,0,sizeof(fctx->f_orginl_name));
	memset(fctx->f_b64_name,0,sizeof(fctx->f_b64_name));
	memset(fctx->f_map_name,0,sizeof(fctx->f_map_name));
	memset(fctx->f_block_map_name,0,sizeof(fctx->f_block_map_name));
	memset(fctx->f_id,0,sizeof(fctx->f_id));
	memset(fctx->f_path_name,0,sizeof(fctx->f_path_name));
	memset(fctx->f_buff,0,fctx->f_buff_size);
	return fctx;
}

void file_ctx_clean(file_ctx *fctx)
{
	assert(fctx != NULL);
	if(fctx->fd > 0)
	{
		close(fctx->fd);
		fctx->fd = -1;
	}
	fctx->f_op_flags = 0;
	fctx->f_mnt_block_index = -1;
	fctx->f_mp_pre = -1;
	fctx->f_mp_suf = -1;
	fctx->f_timestamp = 0;
	fctx->f_crc32 = 0;
	fctx->alloc_count = 0;
	fctx->f_roffset = fctx->f_woffset = fctx->f_buff_offset = 0;
	fctx->f_size = fctx->f_offset = fctx->f_total_offset = fctx->f_total_size = 0;
	fctx->f_op_func = NULL;
	fctx->f_dio_func = NULL;
	fctx->f_cleanup_func = NULL;
	memset(fctx->f_orginl_name,0,sizeof(fctx->f_orginl_name));
	memset(fctx->f_b64_name,0,sizeof(fctx->f_b64_name));
	memset(fctx->f_map_name,0,sizeof(fctx->f_map_name));
	memset(fctx->f_block_map_name,0,sizeof(fctx->f_block_map_name));
	memset(fctx->f_id,0,sizeof(fctx->f_id));
	memset(fctx->f_path_name,0,sizeof(fctx->f_path_name));
	memset(fctx->f_buff,0,fctx->f_buff_size);
}

void file_ctx_reset(file_ctx *fctx)
{
	assert(fctx != NULL);
	fctx->alloc_count = 0;
	fctx->f_roffset = fctx->f_woffset = fctx->f_buff_offset = 0;
	fctx->f_size = fctx->f_offset = fctx->f_total_offset = fctx->f_total_size = 0;
	memset(fctx->f_buff,0,fctx->f_buff_size);
	return;
}

void file_ctx_buff_reset(file_ctx *fctx)
{
	assert(fctx != NULL);
	fctx->f_buff_offset = 0;
	memset(fctx->f_buff,0,fctx->f_buff_size);
	return;
}

void file_ctx_free(file_ctx *fctx)
{
	assert(fctx != NULL);
	if(fctx->f_buff)
	{
		free(fctx->f_buff);
		fctx->f_buff = NULL;
	}
	free(fctx);
	fctx = NULL;
	return;
}

int file_unlink(file_ctx *fctx)
{
	assert(fctx != NULL);
	if(unlink(fctx->f_map_name) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"delete file name %s ,file map name %s failed,errno:%d,"\
				"error info:%s!",\
				__LINE__,fctx->f_orginl_name,fctx->f_map_name,errno,strerror(errno));
		return errno;
	}
	return LFS_OK;
}

int file_metedata_pack(file_ctx *fctx)
{
	assert(fctx != NULL);
	__fileidname_pack(fctx);
	__mapname_pack(fctx);
	__blockmapname_pack(fctx);
	memset(fctx->f_id,0,sizeof(fctx->f_id));
	snprintf(fctx->f_id,sizeof(fctx->f_id),"%s/%s/B%02X/%s",\
			confitems.group_name,\
			confitems.volume_name,\
			fctx->f_mnt_block_index,\
			fctx->f_map_name);
	return LFS_OK;
}

int file_metedata_unpack(const char *file_b64name,file_metedata *fmete)
{
	assert(fmete != NULL);
	char file_name_b64buff[LFS_FILE_NAME_SIZE] = {0};
	char file_binname[256] = {0};
	char *p;

	snprintf(file_name_b64buff,sizeof(file_name_b64buff),"%s",\
			file_b64name);
	Base64decode(file_binname,file_b64name);
	p = file_binname;
	fmete->f_timestamp = (time_t)buff2long((const char*)p);
	p = p + LFS_FILE_METEDATA_TIME_BUFF_SIZE;
	fmete->f_offset = buff2long((const char*)p);
	p = p + LFS_FILE_METEDATA_OFFSET_BUFF_SIZE;
	fmete->f_size = buff2long((const char*)p);
	p = p + LFS_FILE_METEDATA_SIZE_BUFF_SIZE;
	fmete->f_crc32 = buff2int((const char*)p);
	return LFS_OK;
}

int64_t get_filesize_by_name(const char *file_name)
{
	struct stat stat_buf;
	if(lstat(file_name,&stat_buf) == 0)
	{
		if(!S_ISREG(stat_buf.st_mode))
		{
			logger_error("file: "__FILE__", line: %d, " \
					"the block map name %s is not a regular file.",\
					__LINE__,\
					file_name);
			return LFS_ERROR;
		}
		return stat_buf.st_size;
	}
	return LFS_OK;
}

static int __fileidname_pack(file_ctx *fctx)
{
	char f_timestamp_buff[LFS_FILE_METEDATA_TIME_BUFF_SIZE] = {0};
	char f_offset_buff[LFS_FILE_METEDATA_OFFSET_BUFF_SIZE] = {0};
	char f_size_buff[LFS_FILE_METEDATA_SIZE_BUFF_SIZE] = {0};
	char f_crc32_buff[LFS_FILE_METEDATA_CRC32_BUFF_SIZE] = {0};
	char f_name_buff[LFS_FILE_METEDATA_NAME_BUFF_SIZE] = {0};
	char *p;

	memset(fctx->f_b64_name,0,sizeof(fctx->f_b64_name));

	long2buff((long)fctx->f_timestamp,f_timestamp_buff);	
	long2buff(fctx->f_offset,f_offset_buff);
	long2buff(fctx->f_total_size,f_size_buff);
	int2buff(fctx->f_crc32,f_crc32_buff);
	
	p = f_name_buff;
	memcpy(p,f_timestamp_buff,LFS_FILE_METEDATA_TIME_BUFF_SIZE);
	p = p + LFS_FILE_METEDATA_TIME_BUFF_SIZE;
	memcpy(p,f_offset_buff,LFS_FILE_METEDATA_OFFSET_BUFF_SIZE);
	p = p + LFS_FILE_METEDATA_OFFSET_BUFF_SIZE;
	memcpy(p,f_size_buff,LFS_FILE_METEDATA_SIZE_BUFF_SIZE);
	p = p + LFS_FILE_METEDATA_SIZE_BUFF_SIZE;
	memcpy(p,f_crc32_buff,LFS_FILE_METEDATA_CRC32_BUFF_SIZE);
	p = p + LFS_FILE_METEDATA_CRC32_BUFF_SIZE;
	Base64encode(fctx->f_b64_name,(const char*)f_name_buff,p - f_name_buff);
	return LFS_OK;
}

static int __mapname_pack(file_ctx *fctx)
{
	memset(fctx->f_map_name,0,sizeof(fctx->f_map_name));
	sprintf(fctx->f_map_name,"%02X/%02X/%s",(unsigned int)fctx->f_mp_pre,(unsigned int)fctx->f_mp_suf,fctx->f_b64_name);
	return LFS_OK;
}

static int __blockmapname_pack(file_ctx *fctx)
{
	memset(fctx->f_block_map_name,0,sizeof(fctx->f_block_map_name));
	sprintf(fctx->f_block_map_name,"%s/%s",mounts.blocks[fctx->f_mnt_block_index]->mount_path,fctx->f_map_name);
	return LFS_OK;
}
