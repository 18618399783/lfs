/**
*
*
*
*
**/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/socket.h>


#include "common_define.h"
#include "lfs_define.h"
#include "lfs_types.h"
#include "lfs_protocol.h"
#include "lfs_client_types.h"
#include "shared_func.h"
#include "sock_opt.h"

#define DEFAULT_NETWORK_TIMEOUT 30
#define TEST_GROUP_NAME "g1"
#define TEST_VOLUME_NAME "v1"
#define TEST_TIMESTAMP 1480861525  

void usage(char **argv);
int send_write_req(int sfd);
int recv_write_response(int sfd);
int recv_header(int sfd,int64_t *in_bytes);

int main(int argc,char **argv)
{
	int ret = 0;
	int sfd;
	char *pipport;
	char *pport;
	char ip[IP_ADDRESS_SIZE];
	int port;

	if(argc < 2)
	{
		usage(argv);
		return -1;
	}
	pipport = argv[1];
	pport = strchr(pipport,':');
	if(pport == NULL)
	{
		printf("invalid tracker ip address and port:%s\n",pipport);
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
	send_write_req(sfd);
	recv_write_response(sfd);
err:
	close(sfd);
	return ret;
}


void usage(char **argv)
{
	printf("------------------------------\n");
	printf("usage: %s <dataserver_ip:port>\n",argv[0]);
	printf("------------------------------\n");
}

int send_write_req(int sfd)
{
	int ret;
	char req_buff[sizeof(protocol_header) + sizeof(lfs_read_tracker_req)];
	protocol_header *req_header;
	lfs_read_tracker_req *req;

	memset(req_buff,0,sizeof(protocol_header) + sizeof(lfs_read_tracker_req));
	req_header = (protocol_header*)req_buff;
	req_header->header_s.body_len = sizeof(lfs_read_tracker_req);
	req_header->header_s.cmd = PROTOCOL_CMD_READ_TRACKER_REQ;

	req = (lfs_read_tracker_req*)(req_buff + sizeof(protocol_header));
	sprintf(req->map_info,"%s/%s",TEST_GROUP_NAME,TEST_VOLUME_NAME);
	long2buff((int64_t)TEST_TIMESTAMP,req->file_timestamp);
	if((ret = senddata_nblock(sfd,(void*)req_buff,sizeof(req_buff),DEFAULT_NETWORK_TIMEOUT)) != 0)
	{
		printf("send read req to tracker failed.\n");
		return ret; 
	}
	return 0;
}

int recv_write_response(int sfd)
{
	int ret;
	int64_t rbytes;
	int port;
	lfs_tracker_resp *resp;
	char resp_buff[sizeof(lfs_tracker_resp) + 1];

	memset(resp_buff,0,sizeof(lfs_tracker_resp) + 1);
	rbytes = 0;
	if((ret = recv_header(sfd,&rbytes)) != 0)
	{
		printf("recv protocol header data failed,errno:%d.\n",ret);
		return ret; 
	}
	if((ret = recvdata_nblock(sfd,(void*)resp_buff,
					(const int)sizeof(lfs_tracker_resp),DEFAULT_NETWORK_TIMEOUT,NULL)) != 0)
	{
		printf("recv write tracker response failed.\n");
		return ret;
	}
	resp = (lfs_tracker_resp*)resp_buff;
	port = (int)buff2long(resp->block_port);
	printf("datasever ip:%s,port:%d.\n",resp->block_ipaddr,port);
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
