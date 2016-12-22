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
#include <sys/types.h>
#include <sys/statvfs.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "common_define.h"
#include "shared_func.h"
#include "sock_opt.h"
#include "lfs_protocol.h"
#include "lfs_client_types.h"

#define DEFAULT_NETWORK_TIMEOUT 30

void usage(char **argv);
int uploadfile_by_filename(const int sfd,const char *filename);
int uploadfile_do(const int sfd,const char *filename,char *file_data,int64_t file_size);
int recv_header(int sfd,int64_t *in_bytes);

int main(int argc,char **argv)
{
	int sfd;
	char *local_filename;
	char *pipport;
	char *pport;
	char ip[IP_ADDRESS_SIZE];
	int port;
	int ret = 0;

	if(argc < 3)
	{
		usage(argv);
		return -1;
	}
	pipport = argv[1];
	local_filename = argv[2];
	pport = strchr(pipport,':');
	if(pport == NULL)
	{
		printf("invalid dataserver ip address and port:%s\n",pipport);
		return -1;
	}
	snprintf(ip,sizeof(ip),"%.*s",(int)(pport - pipport),pipport);
	port = atoi(pport + 1);
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

	uploadfile_by_filename(sfd,local_filename);
err:
	close(sfd);
	return ret;
}

void usage(char **argv)
{
	printf("------------------------------\n");
	printf("usage: %s <dataserver_ip:port> <upload file name>\n",argv[0]);
	printf("------------------------------\n");
}

int uploadfile_by_filename(const int sfd,const char *filename)
{
	int ret;
	struct stat stat_buf;
	char *file_data = NULL;
	int64_t file_size = 0;

	if(stat(filename,&stat_buf) != 0)
	{
		printf("get filename %s stat failed.\n",filename);
		return errno;
	}
	if(!S_ISREG(stat_buf.st_mode))
	{
		printf("filename %s is not a regular file.\n",filename);
		return -1;
	}

	if((ret = getFileContent(filename,&file_data,(off_t*)&file_size)) != 0)
	{
		printf("read file %s failed.\n",filename);
		return ret;
	}

	uploadfile_do(sfd,filename,file_data,file_size);
	return 0;
}

int uploadfile_do(const int sfd,const char *filename,char *file_data,int64_t file_size)
{
	int ret;
	char req_buff[sizeof(protocol_header) + sizeof(lfs_fileupload_req)];
	char resp_buff[512] = {0};
	int64_t rbytes;
	protocol_header *req_header;
	lfs_fileupload_req *req_body;

	memset(req_buff,0,sizeof(req_buff));
	req_header = (protocol_header*)req_buff;
	req_body = (lfs_fileupload_req*)(req_buff + sizeof(protocol_header));
	req_header->header_s.body_len = sizeof(lfs_fileupload_req);
	req_header->header_s.cmd = PROTOCOL_CMD_FILE_UPLOAD; 

	memcpy(req_body->file_name,filename,strlen(filename));
	long2buff((int64_t)file_size,req_body->file_size);		
	long2buff((int64_t)file_size,req_body->file_total_size);		

	if((ret = senddata_nblock(sfd,(void*)req_buff,sizeof(req_buff),DEFAULT_NETWORK_TIMEOUT)) != 0)
	{
		printf("send upload req data failed.\n");
		return ret; 
	}
	printfBuffHex("req buff:",(const char*)req_buff,(const int)sizeof(req_buff));
#if 0
	if((ret = recv_header(sfd,&rbytes)) != 0)
	{
		printf("recv req ack data failed,errno:%d.\n",ret);
		return ret; 
	}
#endif
	if((ret = senddata_nblock(sfd,(void*)file_data,file_size,DEFAULT_NETWORK_TIMEOUT)) != 0)
	{
		printf("upload data failed.\n");
		return ret; 
	}
	printfBuffHex("file buff:",(const char*)file_data,(const int)file_size);
	memset(resp_buff,0,sizeof(resp_buff));
	rbytes = 0;
	if((ret = recv_header(sfd,&rbytes)) != 0)
	{
		printf("recv file id protocol header data failed,errno:%d.\n",ret);
		return ret; 
	}
	if((ret = recvdata_nblock(sfd,(void*)resp_buff,rbytes,DEFAULT_NETWORK_TIMEOUT,NULL)) != 0)
	{
		printf("recv file id failed,errno:%d.\n",ret);
		return ret; 
	}
	printf("file id:%s\n",resp_buff);
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
