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
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/statvfs.h>

#include "common_define.h"
#include "shared_func.h"
#include "logger.h"
#include "crc32.h"
#include "base64.h"
#include "lfs_client_types.h"
#include "lfs_types.h"
#include "ds_types.h"
#include "ds_conn.h"
#include "ds_file.h"
#include "ds_binlog.h"
#include "ds_sync.h"
#include "ds_block.h"
#include "ds_disk_io.h"
#include "ds_service.h"

static void __resp_header(conn *c,protocol_resp_status resp_status);

void handle_protocol_error(conn *c,protocol_resp_status resp_status)
{
	assert(c != NULL);
	__resp_header(c,resp_status);
	set_conn_state(c,conn_write);
	return;
}

void handle_protocol_ack(conn *c,enum conn_states cstates,enum conn_states nextto)
{
	assert(c != NULL);
	__resp_header(c,PROTOCOL_RESP_STATUS_SUCCESS);
	set_conn_state(c,cstates);
	c->nextto = nextto;
	return;
}

void file_upload_done_callback(conn *c,const int err_no)
{
	assert((c != NULL) && (c->fctx != NULL));
	file_ctx *fctx = c->fctx;
	int ret = err_no;

	if(ret != 0)
	{
		__resp_header(c,PROTOCOL_RESP_STATUS_ERROR_DIO_WRITE_DISK);
		dio_notify_nio(c,conn_write,EV_WRITE|EV_PERSIST);
		return;
	}
	else
	{
		if(fctx->f_total_offset >= fctx->f_total_size)
		{
			struct stat stat_buff;
			if(stat(fctx->f_block_map_name,&stat_buff) == 0)
			{
				fctx->f_create_timestamp = stat_buff.st_mtime;
			}
			else
			{
				logger_warning("file: "__FILE__", line: %d, " \
						"Call stat the original file name %s,block map name %s failed,errno:%d,error info:%s!",\
						__LINE__,fctx->f_orginl_name,fctx->f_block_map_name,errno,STRERROR(errno));
				fctx->f_create_timestamp = time(NULL);
			}
			char old_blockmn[MAX_PATH_SIZE] = {0};
			memcpy(old_blockmn,fctx->f_block_map_name,sizeof(fctx->f_block_map_name));
			fctx->f_crc32 = get_file_crc32_value((const char*)old_blockmn);
			if(file_metedata_pack(c->fctx) != 0)
			{
				logger_error("file: "__FILE__", line: %d, " \
						"Gen file id to the original file %s,block map name %s failed!",\
						__LINE__,c->fctx->f_orginl_name,\
						c->fctx->f_block_map_name);
				__resp_header(c,PROTOCOL_RESP_STATUS_ERROR_FILEMETEDATA_PACK);
				dio_notify_nio(c,conn_write,EV_WRITE|EV_PERSIST);
				return;
			}
			if(rename(old_blockmn,fctx->f_block_map_name) != 0)
			{
				logger_error("file: "__FILE__", line: %d, " \
						"rename old file name %s to new file name %s for the original file %s failed!",\
						__LINE__,old_blockmn,\
						c->fctx->f_block_map_name,\
						c->fctx->f_orginl_name);
				__resp_header(c,PROTOCOL_RESP_STATUS_ERROR_FILEMETEDATA_PACK);
				dio_notify_nio(c,conn_write,EV_WRITE|EV_PERSIST);
				return;

			}
			char binlog_buff[BINLOG_RECORD_SIZE] = {0};
			snprintf(binlog_buff,sizeof(binlog_buff),"%ld%c%c%c%s\n",\
					c->fctx->f_create_timestamp,\
					BAT_DATA_SEPERATOR_SPLITSYMBOL,\
					FILE_OP_TYPE_CREATE_FILE,\
					BAT_DATA_SEPERATOR_SPLITSYMBOL,\
					c->fctx->f_id);
			if(binlog_write(&bctx,binlog_buff) != 0)
			{
				logger_error("file: "__FILE__", line: %d, " \
						"Write binlog  to file id:%s,block map name %s failed!",\
						__LINE__,c->fctx->f_id,\
						c->fctx->f_block_map_name);
				__resp_header(c,PROTOCOL_RESP_STATUS_ERROR_BINLOG_WRITE);
				dio_notify_nio(c,conn_write,EV_WRITE|EV_PERSIST);
				return;
			}
			lfs_fileupload_resp *resp;
			protocol_header *resp_header;
			int fileid_b64_len;

			resp_header = (protocol_header*)c->wbuff;
			resp_header->header_s.body_len = (uint64_t)sizeof(lfs_fileupload_resp);
			resp_header->header_s.cmd = (uint8_t)PROTOCOL_CMD_FILE_UPLOAD;
			resp_header->header_s.state = (uint8_t)PROTOCOL_RESP_STATUS_SUCCESS;
			c->wbytes = sizeof(protocol_header);

			resp = (lfs_fileupload_resp*)(c->wbuff + \
					sizeof(protocol_header));
			base64_encode(&ctxs.b64_ctx,c->fctx->f_id,\
					strlen(c->fctx->f_id),resp->file_b64_id,\
					&fileid_b64_len);
			int2buff(fileid_b64_len,resp->file_b64_id_len);
			c->wbytes += sizeof(lfs_fileupload_resp);
			dio_notify_nio(c,conn_write,EV_WRITE|EV_PERSIST);
			return;
		}
		else if(fctx->f_offset >= fctx->f_size)
		{
			__resp_header(c,PROTOCOL_RESP_STATUS_SUCCESS);
			file_ctx_reset(fctx);
			dio_notify_nio(c,conn_write,EV_WRITE|EV_PERSIST);
			return;
		}
	}
	return;
}

void asyncfile_done_callback(conn *c,const int err_no)
{
	assert((c != NULL) && (c->fctx != NULL));
	int ret = err_no;

	if(ret != 0)
	{
		__resp_header(c,PROTOCOL_RESP_STATUS_ERROR_DIO_WRITE_DISK);
		dio_notify_nio(c,conn_write,EV_WRITE|EV_PERSIST);
		return;
	}
	else
	{
		char binlog_buff[BINLOG_RECORD_SIZE] = {0};
		snprintf(binlog_buff,sizeof(binlog_buff),"%ld%c%c%c%s\n",\
				c->fctx->f_create_timestamp,\
				BAT_DATA_SEPERATOR_SPLITSYMBOL,\
				FILE_OP_TYPE_CREATE_FILE,\
				BAT_DATA_SEPERATOR_SPLITSYMBOL,\
				c->fctx->f_id);
		if(binlog_write(&rbctx,binlog_buff) != 0)
		{
			logger_error("file: "__FILE__", line: %d, " \
					"Write binlog  to file id:%s,block map name %s failed!",\
					__LINE__,c->fctx->f_id,\
					c->fctx->f_block_map_name);
			__resp_header(c,PROTOCOL_RESP_STATUS_ERROR_BINLOG_WRITE);
			dio_notify_nio(c,conn_write,EV_WRITE|EV_PERSIST);
			return;
		}
		protocol_header *resp_header;

		resp_header = (protocol_header*)c->wbuff;
		resp_header->header_s.body_len = 0x00;
		resp_header->header_s.cmd = (uint8_t)PROTOCOL_CMD_ASYNC_COPY_FILE;
		resp_header->header_s.state = (uint8_t)PROTOCOL_RESP_STATUS_SUCCESS;
		c->wbytes = sizeof(protocol_header);

		if(c->fctx->f_create_timestamp > ctxs.last_sync_sequence)
		{
			ctxs.last_sync_sequence = c->fctx->f_create_timestamp;
		}
		dio_notify_nio(c,conn_write,EV_WRITE|EV_PERSIST);
		return;
	}
	return;
}

void file_download_done_callback(conn *c,const int err_no)
{
	int ret = err_no;

	if(ret != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Download file %s failed,error code:%02x.",\
				__LINE__,c->fctx->f_block_map_name,err_no);
		__resp_header(c,PROTOCOL_RESP_STATUS_ERROR_FILE_DOWNLOAD_DIO);
		dio_notify_nio(c,conn_write,EV_WRITE|EV_PERSIST);
		return;
	}
	return;
}

protocol_resp_status handle_cmd_fileupload(conn *c)
{
	protocol_resp_status status = PROTOCOL_RESP_STATUS_SUCCESS;
	lfs_fileupload_req *f_upload_req;

	f_upload_req = (lfs_fileupload_req*)c->rcurr;
	c->fctx = file_ctx_new(c->sfd,DEFAULT_FILE_BUFF_SIZE,FILE_OP_TYPE_WRITE);
	if(c->fctx == NULL)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Allocate file context to file %s failed!",\
			   	__LINE__,f_upload_req->file_name);
		return PROTOCOL_RESP_STATUS_ERROR_MEMORY;
	}
	if(mount_block_map_polling(c) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Get current mount block map info to file %s failed!",\
				__LINE__,f_upload_req->file_name);
		return PROTOCOL_RESP_STATUS_ERROR_MOUNT_BLOCK_MAP_INFO;
	}

	memcpy(c->fctx->f_orginl_name,f_upload_req->file_name,strlen(f_upload_req->file_name));
	c->fctx->f_offset = 0;
	c->fctx->f_size = (int64_t)buff2long(f_upload_req->file_size);
	c->fctx->f_total_size = (int64_t)buff2long(f_upload_req->file_total_size);
	c->fctx->f_op_flags = O_WRONLY | O_CREAT;
	c->fctx->f_crc32 = rand();
	c->fctx->f_create_timestamp = time(NULL);
	c->fctx->f_modify_timestamp = time(NULL);
	c->fctx->f_dio_func = dio_write; 
	c->fctx->f_op_func = file_upload_done_callback; 
	c->fctx->f_cleanup_func = dio_write_error_cleanup; 

	if(file_metedata_pack(c->fctx) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Gen write file id to file %s failed!",\
			   	__LINE__,c->fctx->f_orginl_name);
		return PROTOCOL_RESP_STATUS_ERROR_FILEMETEDATA_PACK;
	}

	set_conn_state(c,conn_fread);
	return status;
}

protocol_resp_status handle_cmd_filedownload(conn *c)
{
	protocol_resp_status status = PROTOCOL_RESP_STATUS_SUCCESS;
	lfs_filedownload_req *f_download_req;
	int64_t file_offset;
	int64_t download_size;

	f_download_req = (lfs_filedownload_req*)c->rcurr;
	c->fctx = file_ctx_new(c->sfd,DEFAULT_FILE_BUFF_SIZE,FILE_OP_TYPE_READ);
	if(c->fctx == NULL)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Allocate file context to file %s failed!",\
			   	__LINE__,f_download_req->file_map_name);
		return PROTOCOL_RESP_STATUS_ERROR_MEMORY;
	}
	file_offset = (int64_t)buff2long(f_download_req->file_offset);
	download_size = (int64_t)buff2long(f_download_req->download_size);	
	c->fctx->f_mnt_block_index = (int)buff2long(f_download_req->mnt_block_index);
	memcpy(c->fctx->f_map_name,f_download_req->file_map_name,strlen(f_download_req->file_map_name));
	sprintf(c->fctx->f_block_map_name,"%s/%s",mounts.blocks[c->fctx->f_mnt_block_index]->mount_path,c->fctx->f_map_name);

	if(file_offset < 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"invalid file offset %ld for download the block map name %s.",\
			   	__LINE__,file_offset,\
				c->fctx->f_block_map_name);
		return PROTOCOL_RESP_STATUS_ERROR_FILEDOWNLOAD_OFFSET_INVALID;
	}
	if(download_size <= 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"invalid download size %ld for the block map name %s.",\
			   	__LINE__,download_size,\
				c->fctx->f_block_map_name);
		return PROTOCOL_RESP_STATUS_ERROR_FILEDOWNLOAD_SIZE_INVALID;
	}
	if((file_offset != 0) && (download_size >= file_offset))
	{
		logger_error("file: "__FILE__", line: %d, " \
				"invalid download file size %ld for the block map name %s.",\
			   	__LINE__,download_size,\
				c->fctx->f_block_map_name);
		return PROTOCOL_RESP_STATUS_ERROR_FILEDOWNLOAD_SIZE_INVALID;
	}
	c->fctx->f_size = download_size;
	c->fctx->f_op_flags = O_RDONLY;
	c->fctx->f_dio_func = dio_read; 
	c->fctx->f_op_func = file_download_done_callback; 

	handle_protocol_ack(c,conn_mwrite,conn_n2dio);
	return status;
}

protocol_resp_status handle_cmd_asynccopyfile(conn *c)
{
	protocol_resp_status status = PROTOCOL_RESP_STATUS_SUCCESS;
	int f_id_length;
	int nblock_index;
	char *pc;

	pc = c->rcurr;
	c->fctx = file_ctx_new(c->sfd,DEFAULT_FILE_BUFF_SIZE,FILE_OP_TYPE_READ);
	if(c->fctx == NULL)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Allocate file context failed!",\
			   	__LINE__);
		return PROTOCOL_RESP_STATUS_ERROR_MEMORY;
	}

	c->fctx->f_create_timestamp = (time_t)buff2long(pc);
	f_id_length = (int)buff2long(pc + LFS_STRUCT_PROP_LEN_SIZE8);
	memcpy(c->fctx->f_id,pc + \
			LFS_STRUCT_PROP_LEN_SIZE8 + \
			LFS_STRUCT_PROP_LEN_SIZE8,f_id_length);
	c->fctx->f_size = (int64_t)buff2long(pc + \
			LFS_STRUCT_PROP_LEN_SIZE8 + \
			LFS_STRUCT_PROP_LEN_SIZE8 + \
			f_id_length);
	LFS_SPLIT_BLOCK_INDEX_BY_FILE_ID(c->fctx->f_id)
	nblock_index = (int)atoi(pblock_index);
	sprintf(c->fctx->f_block_map_name,"%s/%s",mounts.blocks[nblock_index]->mount_path,fid_map_name_buff);
	c->fctx->f_op_flags = O_WRONLY | O_CREAT;
	c->fctx->f_dio_func = dio_write; 
	c->fctx->f_op_func = asyncfile_done_callback; 
	c->fctx->f_cleanup_func = dio_write_error_cleanup; 

	set_conn_state(c,conn_fread);
	return status;
}

protocol_resp_status handle_cmd_getmasterbinlogmete(conn *c)
{
	protocol_resp_status status = PROTOCOL_RESP_STATUS_SUCCESS;
	char *p;
	protocol_header *procl_header;

	procl_header = (protocol_header*)c->wbuff;
	procl_header->header_s.body_len = LFS_STRUCT_PROP_LEN_SIZE4 * 2;
	procl_header->header_s.cmd = (uint8_t)PROTOCOL_CMD_FULL_SYNC_GET_MASTER_BINLOG_METE;
	procl_header->header_s.state = (uint8_t)PROTOCOL_RESP_STATUS_SUCCESS;
	c->wbytes = sizeof(protocol_header);

	p = c->wbuff + sizeof(protocol_header);
	int2buff((const int)(bctx.curr_binlog_file_index + 1),p);
	int2buff((const int)(rbctx.curr_binlog_file_index + 1),\
			p + LFS_STRUCT_PROP_LEN_SIZE4);
	c->wbytes += LFS_STRUCT_PROP_LEN_SIZE4 * 2;
	set_conn_state(c,conn_write);
	return status;
}

protocol_resp_status handle_cmd_copymasterbinlog(conn *c)
{
	protocol_resp_status status = PROTOCOL_RESP_STATUS_SUCCESS;
	int ret;
	char *p;
	char b_fn[BINLOG_FILE_NAME_SIZE] = {0};
	struct stat stat_buff;
	int bindex;
	enum full_sync_binlog_type btype;
	int64_t bfile_size = 0;
	int64_t boffset = 0;
	protocol_header *req_header;

	p = c->rcurr;
	bindex = buff2int((const char*)p);	
	btype = (enum full_sync_binlog_type)buff2int((const char*)p + LFS_STRUCT_PROP_LEN_SIZE4);
	boffset = buff2long((const char*)p + LFS_STRUCT_PROP_LEN_SIZE4 + \
			LFS_STRUCT_PROP_LEN_SIZE4);

	if(btype == LOCAL_BINLOG)
	{
		BINLOG_FILENAME(bctx.binlog_file_name,bindex,b_fn)
	}
	else if(btype == REMOTE_BINLOG)
	{
		BINLOG_FILENAME(rbctx.binlog_file_name,bindex,b_fn)
	}
	else
	{
		logger_warning("file: "__FILE__", line: %d, " \
				"Sync binlog data without binlog type.",\
				__LINE__);
		return PROTOCOL_RESP_STATUS_ERROR_SYNC_NO_BINLOGTYPE;
	}
	
	c->fctx = file_ctx_new(c->sfd,DEFAULT_FILE_BUFF_SIZE,FILE_OP_TYPE_READ);
	if(c->fctx == NULL)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Allocate file context to binlog file %s failed!",\
			   	__LINE__,b_fn);
		return PROTOCOL_RESP_STATUS_ERROR_MEMORY;
	}
	if((ret = stat(b_fn,&stat_buff)) != 0)
	{
		if(ret == ENOENT)
		{
			logger_warning("file: "__FILE__", line: %d, " \
					"The binlog file \"%s\" does not exists.",\
					__LINE__,\
					b_fn);
			status = PROTOCOL_RESP_STATUS_ERROR_SYNC_NO_BINLOGFILE;
			return status;
		}
		else
		{
			logger_error("file: "__FILE__", line: %d," \
					"Get file \"%s\" stat failed,errno:%d," \
					"error info:%s!", __LINE__,b_fn,errno,strerror(errno));
			status = PROTOCOL_RESP_STATUS_ERROR_SYNC_STAT_BINLOGFILE;
			return status;
		}
	}
	bfile_size = stat_buff.st_size - boffset;
	strcpy(c->fctx->f_path_name,b_fn);
	c->fctx->f_size = bfile_size;
	c->fctx->f_offset = boffset;
	req_header = (protocol_header*)c->wbuff;
	req_header->header_s.body_len = LFS_STRUCT_PROP_LEN_SIZE8;
	req_header->header_s.cmd = PROTOCOL_CMD_FULL_SYNC_COPY_MASTER_BINLOG; 
	c->wbytes = sizeof(protocol_header);
	p = c->wbuff + sizeof(protocol_header);
	long2buff(bfile_size,p);
	c->wbytes += LFS_STRUCT_PROP_LEN_SIZE8;
	set_conn_mstate(c,conn_mwrite,conn_fsend);
	return status;
}

protocol_resp_status handle_cmd_copymasterdata(conn *c)
{
	protocol_resp_status status = PROTOCOL_RESP_STATUS_SUCCESS;
	int sfnlen = 0;
	int ret;
	int64_t file_size;
	sync_file_req *sreq;
	struct stat stat_buff;
	char *p;
	protocol_header *resp_header;

	sreq = (sync_file_req*)c->rcurr;
	c->fctx = file_ctx_new(c->sfd,DEFAULT_FILE_BUFF_SIZE,FILE_OP_TYPE_READ);
	if(c->fctx == NULL)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Allocate file context to %s failed!",\
			   	__LINE__,sreq->sync_file_name);
		return PROTOCOL_RESP_STATUS_ERROR_MEMORY;
	}
	sfnlen = (int)buff2int(sreq->sync_file_name_len);
	memcpy(c->fctx->f_path_name,sreq->sync_file_name,sfnlen);
	if((ret = stat(c->fctx->f_path_name,&stat_buff)) != 0)
	{
		if(ret == ENOENT)
		{
			logger_warning("file: "__FILE__", line: %d, " \
					"The data file \"%s\" does not exists.",\
					__LINE__,\
					c->fctx->f_path_name);
			status = PROTOCOL_RESP_STATUS_ERROR_SYNC_NO_DATAFILE;
			return status;
		}
		else
		{
			logger_error("file: "__FILE__", line: %d," \
					"Get data file \"%s\" stat failed,errno:%d," \
					"error info:%s!",\
				   	__LINE__,c->fctx->f_path_name,errno,strerror(errno));
			status = PROTOCOL_RESP_STATUS_ERROR_SYNC_STAT_DATAFILE;
			return status;
		}
	}
	file_size = (int64_t)stat_buff.st_size;
	c->fctx->f_size = file_size;
	c->fctx->f_offset = 0;
	resp_header = (protocol_header*)c->wbuff;
	resp_header->header_s.body_len = LFS_STRUCT_PROP_LEN_SIZE8;
	resp_header->header_s.cmd = PROTOCOL_CMD_FULL_SYNC_COPY_MASTER_DATA; 
	c->wbytes = sizeof(protocol_header);
	p = c->wbuff + sizeof(protocol_header);
	long2buff(file_size,p);
	c->wbytes += LFS_STRUCT_PROP_LEN_SIZE8;
	set_conn_mstate(c,conn_mwrite,conn_fsend);
	return status;
}

static void __resp_header(conn *c,protocol_resp_status resp_status)
{
	assert(c != NULL);
	protocol_header *req_header;
	protocol_header *resp_header;

	req_header = (protocol_header*)c->rbuff;
	resp_header = (protocol_header*)c->wbuff;
	resp_header->header_s.body_len = 0x00;
	resp_header->header_s.cmd = (uint8_t)req_header->header_s.cmd;
	resp_header->header_s.state = (uint8_t)resp_status;
	c->wbytes = sizeof(protocol_header);
	return;
}

