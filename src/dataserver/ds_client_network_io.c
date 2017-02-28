/**
*
*
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
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <sys/types.h>
#include <sys/sendfile.h>
#include <fcntl.h>
#include <event.h>
#include <pthread.h>

#include "common_define.h"
#include "lfs_define.h"
#include "lfs_types.h"
#include "lfs_protocol.h"
#include "logger.h"
#include "shared_func.h"
#include "sock_opt.h"
#include "ds_queue.h"
#include "dataserverd.h"
#include "ds_client_network_io.h"



int client_senddata(int sfd,char *data_buff,const int buff_size)
{
	if(sfd <= 0) return -1;
	int ret = LFS_OK;
	ret = senddata_nblock(sfd,(void*)data_buff,buff_size,confitems.network_timeout); 
	return ret;
}

int client_recvdata(int sfd,char **resp_buff,int64_t *resp_bytes)
{
	int ret = LFS_OK;
	if((ret = client_recvheader(sfd,resp_bytes)) != 0)
		return ret;
	if((*resp_bytes) == 0)
		return LFS_ERROR;
	*resp_buff = (char*)malloc((*resp_bytes) + 1);
	if((*resp_buff) == NULL)
	{
		*resp_bytes = 0;
		logger_error("file:"__FILE__",line:%d," \
				"Allocate response buff length %d memory failed." \
				,__LINE__,((*resp_bytes) + 1));
		return LFS_ERROR;
	}
	memset((*resp_buff),0,(*resp_bytes + 1));
	if((ret = recvdata_nblock(sfd,(void*)(*resp_buff),*resp_bytes,confitems.network_timeout,NULL)) != 0)
	{
		logger_error("file:"__FILE__",line:%d," \
				"Recv response data failed." \
				,__LINE__);
		if(*resp_buff != NULL)
		{
			free(*resp_buff);
			*resp_buff = NULL;
		}
		return ret;
	}
	return ret;
}

int client_recvdata_nomalloc(int sfd,char *resp_buff,const int64_t resp_bytes)
{
	int ret = LFS_OK;
	memset(resp_buff,0,resp_bytes);
	if((ret = recvdata_nblock(sfd,(void*)resp_buff,resp_bytes,confitems.network_timeout,NULL)) != 0)
	{
		logger_error("file:"__FILE__",line:%d," \
				"Recv response data failed,errno:%d,error info: %s" \
				,__LINE__,ret,strerror(ret));
		return ret;
	}
	return ret;
}

int client_recvheader(int sfd,int64_t *in_bytes)
{
	int ret = LFS_OK;
	protocol_header *protcl_header;
	char header_buff[sizeof(protocol_header)];

	memset(header_buff,0,sizeof(protocol_header));
	if((ret = recvdata_nblock(sfd,header_buff,sizeof(protocol_header),confitems.network_timeout,NULL)) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"Recv response protocol header failed, errno:%d,error info: %s",\
				   	__LINE__,ret,strerror(ret));
		*in_bytes = 0;
		return ret;
	}
	protcl_header = (protocol_header*)header_buff;
	if(protcl_header->header_s.state != PROTOCOL_RESP_STATUS_SUCCESS)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"Response status 0x%02X.",\
				   	__LINE__,protcl_header->header_s.state);
		*in_bytes = 0;
		return protcl_header->header_s.state;
	}
	*in_bytes = protcl_header->header_s.body_len;
	if( *in_bytes < 0)
	{
		logger_error("file:"__FILE__",line:%d" \
				"Recv response data size is %d," \
				"is not correct",__LINE__,*in_bytes);
		*in_bytes = 0;
		return LFS_ERROR;
	}
	return LFS_OK;
}

int client_sendfile(int sfd,const char *f_name,const int64_t f_offset,\
		const int64_t f_size,const int timeout)
{
	int fd;
	int64_t send_bytes;
	int ret = LFS_OK;
	int flags;
	int64_t offset;
	int64_t remain_bytes;

	fd = open(f_name,O_RDONLY);
	if(fd < 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"Open file: %s failed, errno:%d,error info: %s.",\
				   	__LINE__,f_name,errno,strerror(errno));
		return errno;
	}
	flags = fcntl(sfd,F_GETFL,0);
	if(flags < 0)
	{
		ret = errno;
		goto err;
	}
	if(flags & O_NONBLOCK)
	{
		if(fcntl(sfd,F_SETFL,flags & ~O_NONBLOCK) == -1)
		{
			ret = errno;
			goto err;
		}
	}
	offset = f_offset;
	remain_bytes = f_size;
	while(remain_bytes > 0)
	{
		send_bytes = sendfile(sfd,fd,&offset,remain_bytes);
		if(send_bytes <= 0)
		{
			ret = errno;
			goto err;
		}
		remain_bytes -= send_bytes;
	}
	if(flags & O_NONBLOCK)
	{
		if(fcntl(sfd,F_SETFL,flags) == -1)
		{
			ret = errno;
		}
	}
err:
	close(fd);
	return ret;
}

int client_recvfile(int sfd,const char *f_name,const int64_t f_offset,\
		const int64_t f_size,const int timeout)
{
	int ret = LFS_OK;
	int fd;
	char buff[LFS_WRITE_BUFF_SIZE];
	int64_t remain_bytes;
	int recv_bytes;
	int written_bytes;
	int oflag = O_WRONLY | O_CREAT;

	if(f_offset <= 0)
	{
		oflag |= O_TRUNC;
	}
	fd = open(f_name,oflag,0644);
	if(fd < 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"Open file: %s failed, errno:%d,error info: %s.",\
				   	__LINE__,f_name,errno,strerror(errno));
		return errno;
	}
	if((ret = lseek(fd,(off_t)f_offset,SEEK_SET)) < 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"Set file: %s offset failed, errno:%d,error info: %s.",\
				   	__LINE__,f_name,errno,strerror(errno));
		return ret;
	}
	written_bytes = 0;
	remain_bytes = f_size;
	memset(buff,0,sizeof(buff));
	while(remain_bytes > 0)
	{
		if(remain_bytes > sizeof(buff))
		{
			recv_bytes = sizeof(buff);
		}
		else
		{
			recv_bytes = remain_bytes;
		}
		memset(buff,0,sizeof(buff));
		if((ret = recvdata_nblock(sfd,(void*)buff,recv_bytes,timeout,NULL)) != 0)
		{
			logger_error("file: "__FILE__", line: %d, " \
					"Reading %s network data failed, errno:%d,error info: %s.", __LINE__,f_name,errno,strerror(errno));
			close(fd);
			unlink(f_name);
			return ret;
		}
		if(write(fd,buff,recv_bytes) != recv_bytes)
		{
			logger_error("file: "__FILE__", line: %d, " \
					"Write %s file data failed,errno:%d,error info: %s.", \
					__LINE__,f_name,errno,strerror(errno));
			close(fd);
			unlink(f_name);
			return LFS_ERROR;
		}
		if(confitems.file_fsync_written_bytes > 0)
		{
			written_bytes += recv_bytes;
			if(written_bytes >= confitems.file_fsync_written_bytes)
			{
				written_bytes = 0;
				if((ret = fsync(fd)) != 0)
				{
					logger_error("file: "__FILE__", line: %d, " \
							"Reflush file %s data to disk failed,errno:%d,error info: %s.", \
							__LINE__,f_name,errno,strerror(errno));
					close(fd);
					unlink(f_name);
					return ret;
				}
			}
		}
		remain_bytes -= recv_bytes;
	}
	close(fd);
	return ret;
}
