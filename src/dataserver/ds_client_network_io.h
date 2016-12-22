/**
*
*
*
*
*
*
**/

#ifndef _DS_CLIENT_NETWORK_IO_H_
#define _DS_CLIENT_NETWORK_IO_H_
#include "common_define.h"

#ifdef __cplusplus
extern "C"{
#endif
int client_recvheader(int sfd,int64_t *in_bytes);
int client_senddata(int sfd,char *data_buff,const int buff_size);
int client_recvdata(int sfd,char **resp_buff,int64_t *resp_bytes);
int client_recvdata_nomalloc(int sfd,char *resp_buff,const int64_t resp_bytes);
int client_sendfile(int sfd,const char *f_name,const int64_t f_offset,\
		const int64_t f_size,const int timeout);
int client_recvfile(int sfd,const char *f_name,const int64_t f_offset,\
		const int64_t f_size,const int timeout);

#ifdef __cplusplus
}
#endif
#endif
