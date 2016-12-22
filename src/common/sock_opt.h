/**
*
*
*
*
*
*
*
**/

#ifndef _SOCK_OPT_H_
#define _SOCK_OPT_H_

#include "common_define.h"

#ifdef __cplusplus
extern "C"{
#endif

int socket_bind(int fd,const char*bind_host,const int port);

int set_sock_opt(int fd,const int timeout);

int set_sock_keepalive(int fd,const int idle_seconds);

int set_noblock(int fd);

int connectserver_nb(int sfd,const char *server_ip,const int server_port,int timeout);

int senddata_nblock(int sfd,void *data,const int size,const int timeout);

int recvdata_nblock(int sfd,void *data,const int size,const int timeout,int *in_bytes);
#ifdef __cplusplus
}
#endif

#endif
