/**
*
*
*
*
*
**/
#include<sys/types.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<errno.h>
#include<unistd.h>

#include<stdio.h>
#include<string.h>
#include<stdlib.h>

#include<event.h>
#include<event2/util.h>

#include "shared_func.h"
#include "common_define.h"
#include "sock_opt.h"
#include "lfs_protocol.h"
#include "lfs_types.h"

#define SEND_BUFF_SIZE 1024
#define RECV_BUFF_SIZE 64


struct event_base* main_base;

static int recv_nblock(int sfd,void *data,const int size,const int timeout,int *in_bytes);
static int tcp_connect_server(const char* server_ip, int port);
static void send_test_reqmsg(int sockfd);
static void recv_test_respmsg(int sockfd);

int main(int argc, char** argv)
{
	if( argc < 3 )
	{
		printf("please input 2 parameter\n");
		return -1;
	}

	int sockfd = tcp_connect_server(argv[1], atoi(argv[2]));
	if( sockfd == -1)
	{
		printf("tcp_connect error\n");
		return -1;
	}
	printf("connect to server successful\n");

	send_test_reqmsg(sockfd);
	sleep(1);
	recv_test_respmsg(sockfd);
	//main_base = event_base_new();
	//event_base_dispatch(main_base);

	close(sockfd);
	printf("finished\n");
	return 0;
}


static void send_test_reqmsg(int sockfd)
{
	int ret = 0;
	char *package_body = "test noblock libevent sokcet";

	char req_buff[sizeof(protocol_header) + strlen(package_body)];
	protocol_header *pro_header;
	memset(req_buff,0,sizeof(req_buff));
	pro_header = (protocol_header*)req_buff;

	pro_header->header_s.body_len = strlen(package_body);
	pro_header->header_s.cmd = PROTOCOL_CMD_TEST;


	if((ret = senddata_nblock(sockfd,(void*)req_buff,sizeof(req_buff),30) != 0))
	{
		printf("send msg failed\n");
	}
}

static void recv_test_respmsg(int sockfd)
{
	int ret = 0;
	int in_bytes;
	char msg[40];

	printf("recv dataing....\n");
	if((ret = recv_nblock(sockfd,(void*)msg,40,30,&in_bytes)) != 0)
	{
		printf("errnu:%d\n",ret);
		printf("recv msg failed,recv bytes:%d\n",in_bytes);
	}
	printfBuffHex("recv data:",(const char*)msg,in_bytes);
}

static int tcp_connect_server(const char* server_ip, int port)
{
	int sockfd;

	sockfd = socket(PF_INET, SOCK_STREAM, 0);
	if( sockfd == -1 )
		return sockfd;
	if(set_noblock(sockfd) != 0)
	{
		printf("set none block failed.\n");
		return -1;
	}
	if(set_sock_opt(sockfd,30) != 0)
	{
		printf("set sock opt failed.\n");
		return -1;
	}
	if(connectserver_nb(sockfd,server_ip,port,30) != 0)
	{
		printf("connect failed.\n");
		return -1;
	}

	return sockfd;
}

static int recv_nblock(int sfd,void *data,const int size,const int timeout,int *in_bytes)
{
	int left_bytes;
	int read_bytes;
	int res;
	int ret_code;
	unsigned char* p;
	fd_set read_set;
	struct timeval t;

	FD_ZERO(&read_set);
	FD_SET(sfd, &read_set);

	read_bytes = 0;
	ret_code = 0;
	p = (unsigned char*)data;
	left_bytes = size;
	while (left_bytes > 0)
	{

		if (timeout <= 0)
		{
			res = select(sfd+1, &read_set, NULL, NULL, NULL);
		}
		else
		{
			t.tv_usec = 0;
			t.tv_sec = timeout;
			res = select(sfd+1, &read_set, NULL, NULL, &t);
		}

		if (res < 0)
		{
			printf("---->res < 0,errno:%d.\n",errno);
			ret_code = errno != 0 ? errno : EINTR;
			break;
		}
		else if (res == 0)
		{
			printf("---->res = 0,errno:%d.\n",errno);
			ret_code = ETIMEDOUT;
			break;
		}

		read_bytes = recv(sfd, p, left_bytes, 0);
		if (read_bytes < 0)
		{
			printf("---->read_bytes < 0,errno:%d.\n",errno);
			ret_code = errno != 0 ? errno : EINTR;
			break;
		}
		if (read_bytes == 0)
		{
			printf("---->read_bytes == 0,errno:%d.\n",errno);
			ret_code = ENOTCONN;
			break;
		}
		printf("----->has read bytes:%d.\n",read_bytes);
		left_bytes -= read_bytes;
		p += read_bytes;
	}

	if (in_bytes != NULL)
	{
		*in_bytes = size - left_bytes;
	}
	return ret_code;
}
