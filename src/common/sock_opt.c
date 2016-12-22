/**
*
*
*
*
*
*
**/
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "logger.h"
#include "sock_opt.h"

int socket_bind(int fd,const char*bind_host,const int port)
{
	struct sockaddr_in bindaddr;

	bindaddr.sin_family = AF_INET;
	bindaddr.sin_port = htons(port);
	if((bind_host == NULL) || ((*bind_host) == '\0'))
	{
		bindaddr.sin_addr.s_addr = INADDR_ANY;
	}
	else
	{
		if(inet_aton(bind_host,&bindaddr.sin_addr) == 0)
		{
			logger_error("file: "__FILE__", line: %d, " \
						"Invalid ip addr: \
						%s!", __LINE__,bind_host);
			return -1;
		}
	}
	if(bind(fd,(struct sockaddr*)&bindaddr,sizeof(bindaddr)) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"Failed to bind port %d,errno:%d,error info:\
					 %s!", __LINE__,port,errno,STRERROR(errno));
		return -2;
	}
	return 0;
}

int set_sock_opt(int fd,const int timeout)
{
	int flags = 1;
	int ret = 0;
	struct linger ling;
	struct timeval waittime;

	ling.l_onoff = 1;
	ling.l_linger = timeout;
	if(setsockopt(fd,SOL_SOCKET,SO_LINGER,(void*)&ling,(socklen_t)sizeof(struct linger)) != 0)
	{
		logger_warning("file: "__FILE__", line: %d, " \
					"Failed to setsockopt,errno:%d,error info:\
					 %s!", __LINE__,errno,STRERROR(errno));
		return -1;
	}
	waittime.tv_sec = timeout;
	waittime.tv_usec = 0;
	if(setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,(void*)&waittime,(socklen_t)sizeof(struct timeval)) != 0)
	{
		logger_warning("file: "__FILE__", line: %d, " \
					"Failed to setsockopt,errno:%d,error info:\
					 %s!", __LINE__,errno,STRERROR(errno));
		return -1;
	}
	if(setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,(void*)&waittime,(socklen_t)sizeof(struct timeval)) != 0)
	{
		logger_warning("file: "__FILE__", line: %d, " \
					"Failed to setsockopt,errno:%d,error info:\
					 %s!", __LINE__,errno,STRERROR(errno));
		return -1;
	}
	if(setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,(void*)&flags,sizeof(flags)) != 0)
	{
		logger_warning("file: "__FILE__", line: %d, " \
					"Failed to setsockopt,errno:%d,error info:\
					 %s!", __LINE__,errno,STRERROR(errno));
		return -1;
	}
	if((ret = (set_sock_keepalive(fd,2 * timeout + 1))) != 0)
		return ret;
		
	return 0;
}

int set_sock_keepalive(int fd,const int idle_seconds)
{
	int keepalive;
	int keepidle;
	int keepinterval;
	int keepcount;

	keepalive = 1;
	if(setsockopt(fd,SOL_SOCKET,SO_KEEPALIVE,(char*)&keepalive,sizeof(keepalive)) != 0)
	{
		logger_warning("file: "__FILE__", line: %d, " \
					"Failed to setsockopt,errno:%d,error info:\
					 %s!", __LINE__,errno,STRERROR(errno));
		return -1;
	}
	keepidle = idle_seconds; 
	if(setsockopt(fd,SOL_TCP,TCP_KEEPIDLE,(char*)&keepidle,sizeof(keepidle)) != 0)
	{
		logger_warning("file: "__FILE__", line: %d, " \
					"Failed to setsockopt,errno:%d,error info:\
					 %s!", __LINE__,errno,STRERROR(errno));
		return -1;
	}
	keepinterval = 10;
	if(setsockopt(fd,SOL_TCP,TCP_KEEPINTVL,(char*)&keepinterval,sizeof(keepinterval)) != 0)
	{
		logger_warning("file: "__FILE__", line: %d, " \
					"Failed to setsockopt,errno:%d,error info:\
					 %s!", __LINE__,errno,STRERROR(errno));
		return -1;
	}
	keepcount = 3;
	if(setsockopt(fd,SOL_TCP,TCP_KEEPCNT,(char*)&keepcount,sizeof(keepcount)) != 0)
	{
		logger_warning("file: "__FILE__", line: %d, " \
					"Failed to setsockopt,errno:%d,error info:\
					 %s!", __LINE__,errno,STRERROR(errno));
		return -1;
	}

	return 0;
}

int set_noblock(int fd)
{
	int flags;

	flags = fcntl(fd, F_GETFL);
	if (flags < 0)
		return flags;
	flags |= O_NONBLOCK;
	if (fcntl(fd, F_SETFL, flags) < 0)
		return -1;
	return 0;
}

int connectserver_nb(int sfd,const char *server_ip,const int server_port,const int timeout)
{
	int result;
	socklen_t len;
	fd_set rset;
	fd_set wset;
	struct timeval tval;
	struct sockaddr_in addr;

	addr.sin_family = PF_INET;
	addr.sin_port = htons(server_port);
	result = inet_aton(server_ip, &addr.sin_addr);
	if (result == 0 )
	{
		return EINVAL;
	}

	do
	{
		if (connect(sfd, (const struct sockaddr*)&addr, sizeof(addr)) < 0)
		{
			result = errno != 0 ? errno : EINPROGRESS;
			if (result != EINPROGRESS)
			{
				break;
			}
		}
		else
		{
			result = 0;
			break;
		}

		FD_ZERO(&rset);
		FD_ZERO(&wset);
		FD_SET(sfd,&rset);
		FD_SET(sfd,&wset);
		tval.tv_sec = timeout;
		tval.tv_usec = 0;

		result = select(sfd+1, &rset, &wset, NULL, timeout > 0 ? &tval : NULL);

		if (result == 0)
		{
			result = ETIMEDOUT;
			break;
		}
		else if (result < 0)
		{
			result = errno != 0 ? errno : EINTR;
			break;
		}

		len = sizeof(result);
		if (getsockopt(sfd, SOL_SOCKET, SO_ERROR, &result, &len) < 0)
		{
			result = errno != 0 ? errno : EACCES;
			break;
		}
	} while (0);

	return result;
}

int senddata_nblock(int sfd,void *data,const int size,const int timeout)
{
	int left_bytes;
	int write_bytes;
	int result;
	unsigned char* p;
	fd_set write_set;
	struct timeval t;

	FD_ZERO(&write_set);
	FD_SET(sfd, &write_set);

	p = (unsigned char*)data;
	left_bytes = size;
	while (left_bytes > 0)
	{
		write_bytes = send(sfd, p, left_bytes, 0);
		if (write_bytes < 0)
		{
			if (!(errno == EAGAIN || errno == EWOULDBLOCK))
			{
				return errno != 0 ? errno : EINTR;
			}
		}
		else
		{
			left_bytes -= write_bytes;
			p += write_bytes;
			continue;
		}

		if (timeout <= 0)
		{
			result = select(sfd+1, NULL, &write_set, NULL, NULL);
		}
		else
		{
			t.tv_usec = 0;
			t.tv_sec = timeout;
			result = select(sfd+1, NULL, &write_set, NULL, &t);
		}

		if (result < 0)
		{
			return errno != 0 ? errno : EINTR;
		}
		else if (result == 0)
		{
			return ETIMEDOUT;
		}
	}
	return 0;
}

int recvdata_nblock(int sfd,void *data,const int size,const int timeout,int *in_bytes)
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
			ret_code = errno != 0 ? errno : EINTR;
			break;
		}
		else if (res == 0)
		{
			ret_code = ETIMEDOUT;
			break;
		}

		read_bytes = recv(sfd, p, left_bytes, 0);
		if (read_bytes < 0)
		{
			ret_code = errno != 0 ? errno : EINTR;
			break;
		}
		if (read_bytes == 0)
		{
			ret_code = ENOTCONN;
			break;
		}

		left_bytes -= read_bytes;
		p += read_bytes;
	}

	if (in_bytes != NULL)
	{
		*in_bytes = size - left_bytes;
	}
	return ret_code;
}
