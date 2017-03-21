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
#include <sys/sendfile.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <wait.h>

#include "common_define.h"
#include "shared_func.h"
#include "sock_opt.h"
#include "lfs_protocol.h"
#include "lfs_client_types.h"

#define DEFAULT_NETWORK_TIMEOUT 30
#define WORK_PROCESS_NUMBER 10

void usage(char **argv);
int recv_header(int sfd,int64_t *in_bytes);
int uploadfile_sendbigfile(const int sfd,const char *filename,\
		const int64_t offset,const int64_t filesize,const int timeout);
int uploadfile_by_filename(const int sfd,const char *filename);
int uploadfile_do(const int sfd,const char *filename,int64_t file_size);
int conn_server(const char *ip,const int port);
void fork_test(int number,const char*ip,const int port,const char* filename);


int main(int argc,char **argv)
{
	char *local_filename;
	char *pipport;
	char *pport;
	char ip[IP_ADDRESS_SIZE];
	int port;
	int tnumber = 0;
	int ret = 0;
	time_t timep; 

	if(argc < 4)
	{
		usage(argv);
		return -1;
	}
	pipport = argv[1];
	local_filename = argv[2];
	tnumber = atoi(argv[3]);
	pport = strchr(pipport,':');
	if(pport == NULL)
	{
		printf("invalid dataserver ip address and port:%s\n",pipport);
		return -1;
	}
	snprintf(ip,sizeof(ip),"%.*s",(int)(pport - pipport),pipport);
	port = atoi(pport + 1);

	time (&timep); 
	printf("%s",asctime(gmtime(&timep)));
	fork_test(tnumber,(const char*)ip,(const int)port,(const char*)local_filename);
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
	struct stat stat_buf;
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
	file_size = stat_buf.st_size;
	uploadfile_do(sfd,filename,file_size);
	return 0;
}

int uploadfile_do(const int sfd,const char *filename,int64_t file_size)
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

	//printfBuffHex("protocol header:",(const char*)req_buff,(const int)sizeof(req_buff));
	if((ret = senddata_nblock(sfd,(void*)req_buff,sizeof(req_buff),DEFAULT_NETWORK_TIMEOUT)) != 0)
	{
		printf("send upload req data failed.\n");
		return ret; 
	}

#if 0
	printfBuffHex("req buff:",(const char*)req_buff,(const int)sizeof(req_buff));
#endif

	if((ret = uploadfile_sendbigfile((const int)sfd,filename,0,\
					(const int64_t)file_size,(const int)DEFAULT_NETWORK_TIMEOUT)) != 0)
	{
		printf("send file \'%s\' data failed.\n",filename);
		return ret;
	}

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

int conn_server(const char *ip,const int port)
{
	int sfd;
	int ret;
	sfd = socket(AF_INET,SOCK_STREAM,0);
	if(sfd < 0)
	{
		printf("socket create failed,errno:%d,error info:%s\n",\
				errno,strerror(errno));
		return -1;
	}
	if((ret = set_noblock(sfd)) != 0)
	{
		printf("set socket non block failed.\n");
		return -1;
	}
	if((ret = connectserver_nb(sfd,ip,port,0)) != 0)
	{
		printf("connect server %s:%d failed.\n",ip,port);
		return -1;
	}
	return sfd;
}

void fork_test(int number,const char*ip,const int port,const char* filename)
{
	pid_t pid;
	pid = fork();
	if(pid == 0)
	{
		printf("child pid:%d is uploading.\n",getpid());
		int ret;
		int sfd = conn_server(ip,port);
		if(sfd <= 0)
		{
			printf("connect server failes.\n");
			exit(0);
		}
		if((ret = uploadfile_by_filename(sfd,filename)) < 0)
		{
			printf("pid:%d upload file fails.\n",getpid());
		}
		return;
	}
	//wait(NULL);
	if(number > 0)
	{
		fork_test(number - 1,ip,port,filename);
	}
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

int uploadfile_sendbigfile(const int sfd,const char *filename,\
		const int64_t fileoffset,const int64_t filesize,const int timeout)
{
	int fd;
	int64_t send_bytes;
	int ret;
	int flags;
	int64_t offset;
	int64_t remain_bytes;

	fd = open(filename,O_RDONLY);
	if(fd < 0)
	{
		printf("Open file: %s failed, errno:%d,error info: %s.",\
				   	filename,errno,strerror(errno));
		return errno;
	}
	flags = fcntl(sfd,F_GETFL,0);
	if(flags < 0)
	{
		close(fd);
		return errno;
	}
	if(flags & O_NONBLOCK)
	{
		if(fcntl(sfd,F_SETFL,flags & ~O_NONBLOCK) == -1)
		{
			close(fd);
			return errno;
		}
	}
	offset = fileoffset;
	remain_bytes = filesize;
	while(remain_bytes > 0)
	{
		send_bytes = sendfile(sfd,fd,&offset,remain_bytes);
		if(send_bytes < 0)
		{
			ret = errno;
			break;
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
	close(fd);
	return ret;
}
