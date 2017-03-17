/**
*
*
*
*
*
*
**/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/statvfs.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "common_define.h"
#include "base64.h"
#include "shared_func.h"
#include "sock_opt.h"
#include "ds_types.h"
#include "lfs_define.h"
#include "lfs_types.h"
#include "lfs_protocol.h"
#include "lfs_client_types.h"


base64_context b64_ctx;

void usage(char **argv);
int downloadfile_req(const int sfd,const char *file_id,\
		const char *local_filename);
int downloadfile_req_do(const int sfd,const char *local_filename,\
		const char *volume_name,const char *block_index,\
		const char *fid_map_name);
int downloadfile_to_file(const int sfd,const char *local_filename,\
		const int64_t file_size);
int recv_header(int sfd,int64_t *in_bytes);
int file_metedata_unpack(char *file_b64name,const int file_b64name_len,file_metedata *fmete);

int main(int argc,char **argv)
{
	int sfd;
	char *remote_name;
	char *local_filename;
	char *pipport;
	char *pport;
	char ip[IP_ADDRESS_SIZE];
	int port;
	int ret = 0;

	if(argc < 4)
	{
		usage(argv);
		return -1;
	}
	pipport = argv[1];
	remote_name = argv[2];	
	local_filename = argv[3];
	pport = strchr(pipport,':');
	if(pport == NULL)
	{
		printf("invalid dataserver ip address and port:%s\n",pipport);
		return -1;
	}
	base64_init(&b64_ctx,0);
	snprintf(ip,sizeof(ip),"%.*s",(int)(pport - pipport),pipport);
	port = atoi(pport + 1);
#if 1
	sfd = socket(AF_INET,SOCK_STREAM,0);
	if(sfd < 0)
	{
		printf("socket create failed,errno:%d,error info:%s\n",\
				errno,strerror(errno));
		return errno;
	}
	if((ret = set_noblock(sfd)) != 0)
	{
		printf("set socket non block failed.\n");
		goto err;
	}
	if((ret = connectserver_nb(sfd,ip,port,0)) != 0)
	{
		printf("connect server %s:%d failed.\n",ip,port);
		goto err;
	}
#endif
	downloadfile_req(sfd,remote_name,local_filename);
err:
	close(sfd);
	return ret;
}

void usage(char **argv)
{
	printf("------------------------------\n");
	printf("usage: %s <dataserver_ip:port> <remote name>  <file name>\n",argv[0]);
	printf("------------------------------\n");
}

int downloadfile_req(const int sfd,const char *file_id,const char *local_filename)
{
	int ret = 0;
	char b64decode[256] = {0};
	int fileid_len;

	base64_decode(&b64_ctx,file_id,(const int)strlen(file_id),b64decode,&fileid_len);
	printf("file id:%s\n",b64decode);

	LFS_SPLIT_VOLUME_NAME_AND_BLOCK_INDEX_BY_FILE_ID(b64decode)
	ret = downloadfile_req_do(sfd,local_filename,\
			volume_name,pblock_index,fid_map_name_buff);
	return ret;
}

int downloadfile_req_do(const int sfd,const char *local_filename,\
		const char *volume_name,const char *block_index,\
		const char *fid_map_name)
{
	int ret;
	int64_t download_size = 0;
	char req_buff[sizeof(protocol_header) + sizeof(lfs_filedownload_req)];
	//char resp_buff[512] = {0};
	int64_t rbytes;
	file_metedata fmete;
	protocol_header *req_header;
	lfs_filedownload_req *req_body;
	//lfs_filedownload_resp *resp_body;

	LFS_SPLIT_FILE_MAP_NAME(fid_map_name)
	if(file_metedata_unpack(f_name_buff,(const int)strlen(f_name_buff),&fmete) != 0)
	{
		printf("Unpack file mete data error.\n");
		return -1;
	}
	memset(req_buff,0,sizeof(req_buff));
	req_header = (protocol_header*)req_buff;
	req_body = (lfs_filedownload_req*)(req_buff + sizeof(protocol_header));
	req_header->header_s.body_len = sizeof(lfs_filedownload_req);
	req_header->header_s.cmd = PROTOCOL_CMD_FILE_DOWNLOAD; 

	long2buff(0,req_body->file_offset);		
	long2buff((int64_t)fmete.f_size,req_body->download_size);
	memcpy(req_body->mnt_block_index,block_index,strlen(block_index));
	memcpy(req_body->file_map_name,fid_map_name,strlen(fid_map_name));
	download_size = fmete.f_size;

	printfBuffHex("req buff:",(const char*)req_buff,(const int)sizeof(req_buff));
	if((ret = senddata_nblock(sfd,(void*)req_buff,sizeof(req_buff),DEFAULT_NETWORK_TIMEOUT)) != 0)
	{
		printf("send upload req data failed.\n");
		return ret; 
	}
	if((ret = recv_header(sfd,&rbytes)) != 0)
	{
		printf("recv download file response header failed,errno:%d.\n",ret);
		return ret; 
	}
#if 0
	if((ret = recvdata_nblock(sfd,(void*)resp_buff,rbytes,DEFAULT_NETWORK_TIMEOUT,NULL)) != 0)
	{
		printf("recv download file response data failed,errno:%d.\n",ret);
		return ret; 
	}
	resp_body = (lfs_filedownload_resp*)resp_buff;
	download_size = (int64_t)buff2long(resp_body->download_size);
#endif
	if((ret = downloadfile_to_file(sfd,local_filename,download_size)))
	{
		printf("download file %s failed.\n",local_filename);
		return ret;
	}
	return 0;
}

int downloadfile_to_file(const int sfd,const char *local_filename,\
		const int64_t file_size)
{
	int ret;
	int fd;
	char buff[LFS_WRITE_BUFF_SIZE];
	int64_t remain_bytes;
	int recv_bytes;

	fd = open(local_filename,O_WRONLY | O_CREAT | O_TRUNC,0644);
	if(fd < 0)
	{
		printf("create file %s failed.\n",local_filename);
		return -1;
	}
	remain_bytes = file_size;
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
		if((ret = recvdata_nblock(sfd,(void*)buff,recv_bytes,DEFAULT_NETWORK_TIMEOUT,NULL)) != 0)
		{
			printf("recv status:%d.\n",ret);
			close(fd);
			unlink(local_filename);
			return ret;
		}
		if(write(fd,buff,recv_bytes) != recv_bytes)
		{
			close(fd);
			unlink(local_filename);
			return ret;
		}
		//printfBuffHex("data:",(const char*)buff,(const int)recv_bytes);
		remain_bytes -= recv_bytes;
	}
	close(fd);
	return 0;
}

int recv_header(int sfd,int64_t *in_bytes)
{
	int ret = 0;
	protocol_header *protcl_header;
	char header_buff[sizeof(protocol_header)];

	memset(header_buff,0,sizeof(protocol_header));
	if((ret = recvdata_nblock(sfd,header_buff,sizeof(protocol_header),DEFAULT_NETWORK_TIMEOUT,NULL)) != 0)
	{
		printf("Server fd:%d recv response protocol header failed, errno:%d,error info: %s.\n",sfd,ret,strerror(ret));
		*in_bytes = 0;
		return ret;
	}
	protcl_header = (protocol_header*)header_buff;
	if(protcl_header->header_s.state != PROTOCOL_RESP_STATUS_SUCCESS)
	{
		printf("Server fd:%d response status 0x%02x.\n",sfd,protcl_header->header_s.state);
		*in_bytes = 0;
		return protcl_header->header_s.state;
	}
	*in_bytes = protcl_header->header_s.body_len;
	if( *in_bytes < 0)
	{
		printf("server fd:%d,recv response data size is %d," \
				"is not correct.\n",sfd,*in_bytes);
		*in_bytes = 0;
		return -1;
	}
	return 0;
}

int file_metedata_unpack(char *file_b64name,const int file_b64name_len,file_metedata *fmete)
{
	char file_binname[256] = {0};
	int file_binname_len;
	char *p;

	base64_decode(&b64_ctx,file_binname,file_b64name_len,file_binname,&file_binname_len);
	p = file_binname;
	p = p + LFS_MACHINE_ID_BUFF_SIZE;
	fmete->f_create_timestamp = (time_t)buff2long((const char*)p);
	p = p + LFS_FILE_METEDATA_TIME_BUFF_SIZE;
	fmete->f_modify_timestamp = (time_t)buff2long((const char*)p);
	p = p + LFS_FILE_METEDATA_TIME_BUFF_SIZE;
	fmete->f_offset = buff2long((const char*)p);
	p = p + LFS_FILE_METEDATA_OFFSET_BUFF_SIZE;
	fmete->f_size = buff2long((const char*)p);
	p = p + LFS_FILE_METEDATA_SIZE_BUFF_SIZE;
	fmete->f_crc32 = buff2int((const char*)p);
	return LFS_OK;
}
