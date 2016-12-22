/**
*
*
*
*
*
**/

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/file.h>
#include <dirent.h>
#include <grp.h>
#include <pwd.h>
#include <termios.h>
#include <pthread.h>


#include "shared_func.h"
#include "logger.h"

char* strtok_r (char *s, const char *delim, char **save_ptr)
{
	char *token;

	if (s == NULL)
		s = *save_ptr;

	s += strspn (s, delim);
	if (*s == '\0')
	{
		*save_ptr = s;
		return NULL;
	}

	token = s;
	s = strpbrk (token, delim);
	if (s == NULL)
		*save_ptr = __rawmemchr (token, '\0');
	else
	{
		*s = '\0';
		*save_ptr = s + 1;
	}
	return token;
}

char *formatDatetime(const time_t nTime, \
			const char *szDateFormat, \
			char *buff, const int buff_size)
{
	static char szDateBuff[128];
	struct tm *tmTime;
	int size;

	tmTime = localtime(&nTime);
	if (buff == NULL)
	{
		buff = szDateBuff;
		size = sizeof(szDateBuff);
	}
	else
	{
		size = buff_size;
	}

	buff[0] = '\0';
	strftime(buff, size, szDateFormat, tmTime);

	return buff;
}

int getCharLen(const char *s)
{
	unsigned char *p;
	int count = 0;

	p = (unsigned char *)s;
	while (*p != '\0')
	{
		if (*p > 127)
		{
			if (*(++p) != '\0')
			{
				p++;
			}
		}
		else
		{
			p++;
		}

		count++;
	}

	return count;
}


char *toLowercase(char *src)
{
	char *p;

	p = src;
	while (*p != '\0')
	{
		if (*p >= 'A' && *p <= 'Z')
		{
			*p += 32;
		}
		p++;
	}

	return src;
}

char *toUppercase(char *src)
{
	char *p;

	p = src;
	while (*p != '\0')
	{
		if (*p >= 'a' && *p <= 'z')
		{
			*p -= 32;
		}
		p++;
	}

	return src;	
}

int daemon_init(bool bCloseFiles)
{
	if(!bCloseFiles)
	  return -1;
	int fd;

	switch (fork()) {
		case -1:
			return (-1);
		case 0:
			break;
		default:
			_exit(EXIT_SUCCESS);
	}

	if (setsid() == -1)
		return (-1);

	if(chdir("/") != 0) {
		perror("chdir");
		return (-1);
	}

	if ((fd = open("/dev/null", O_RDWR, 0)) != -1) {
		if(dup2(fd, STDIN_FILENO) < 0) {
			perror("dup2 stdin");
			return (-1);
		}
		if(dup2(fd, STDOUT_FILENO) < 0) {
			perror("dup2 stdout");
			return (-1);
		}
		if(dup2(fd, STDERR_FILENO) < 0) {
			perror("dup2 stderr");
			return (-1);
		}

		if (fd > STDERR_FILENO) {
			if(close(fd) < 0) {
				perror("close");
				return (-1);
			}
		}
	}
	return (0);
}

char *bin2hex(const char *s, const int len, char *szHexBuff)
{
	unsigned char *p;
	unsigned char *pEnd;
	int nLen;

	nLen = 0;
	pEnd = (unsigned char *)s + len;
	for (p=(unsigned char *)s; p<pEnd; p++)
	{
		nLen += sprintf(szHexBuff + nLen, "%02x", *p);
	}

	szHexBuff[nLen] = '\0';
	return szHexBuff;
}

char *hex2bin(const char *s, char *szBinBuff, int *nDestLen)
{
	char buff[3];
	char *pSrc;
	int nSrcLen;
	char *pDest;
	char *pDestEnd;

	nSrcLen = strlen(s);
	if (nSrcLen == 0)
	{
		*nDestLen = 0;
		szBinBuff[0] = '\0';
		return szBinBuff;
	}

	*nDestLen = nSrcLen / 2;
	pSrc = (char *)s;
	buff[2] = '\0';

	pDestEnd = szBinBuff + (*nDestLen);
	for (pDest=szBinBuff; pDest<pDestEnd; pDest++)
	{
		buff[0] = *pSrc++;
		buff[1] = *pSrc++;
		*pDest = (char)strtol(buff, NULL, 16);
	}

	*pDest = '\0';
	return szBinBuff;
}

void hex2ascii(unsigned char * pHex, unsigned char * pAscii, int nLen) 
{
	unsigned char Nibble[2];
	int i;
	int j;
	for (i = 0; i < nLen; i++) {
		Nibble[0] = (pHex[i] & 0xF0) >> 4;
		Nibble[1] = pHex[i] & 0x0F;
		for (j = 0; j < 2; j++) {
			if (Nibble[j] < 10)
			  Nibble[j] += 0x30;
			else {
				if (Nibble[j] < 16)
				  Nibble[j] = Nibble[j] - 10 + 'A';
			}
			*pAscii++ = Nibble[j];
		} // for (int j = ...)
	} // for (int i = ...)
}

int ascii2hex(unsigned char * pAscii, unsigned char * pHex, int nLen,int *nDestLen) 
{
	if (nLen % 2)
	  return -1;
	int nHexLen = nLen / 2;
	*nDestLen = nHexLen;
	int i;
	int j;
	for (i = 0; i < nHexLen; i++) {
		unsigned char Nibble[2];
		Nibble[0] = *pAscii++;
		Nibble[1] = *pAscii++;
		for (j = 0; j < 2; j++) {
			if (Nibble[j] <= 'F' && Nibble[j] >= 'A')
			  Nibble[j] = Nibble[j] - 'A' + 10;
			else if (Nibble[j] <= 'f' && Nibble[j] >= 'a')
			  Nibble[j] = Nibble[j] - 'a' + 10;
			else if (Nibble[j] >= '0' && Nibble[j] <= '9')
			  Nibble[j] = Nibble[j] - '0';
			else
			  return -1;
		} // for (int j = ...)
		pHex[i] = Nibble[0] << 4; // Set the high nibble
		pHex[i] |= Nibble[1]; // Set the low nibble
	} // for (int i = ...)
	return 0;
}

void int16_t2buff(const int16_t n,char *buff)
{
	unsigned char *p;
	p = (unsigned char*)buff;
	*p++ = (n >> 8) & 0xFF;
	*p++ = n & 0xFF;
}

int16_t buff2int16_t(const char *buff)
{
	unsigned char *p;
	p = (unsigned char *)buff;
	return  (((int16_t)(*p)) << 8) | \
		((int16_t)(*(p+1)));
}

void int2buff(const int n, char *buff)
{
	unsigned char *p;
	p = (unsigned char *)buff;
	*p++ = (n >> 24) & 0xFF;
	*p++ = (n >> 16) & 0xFF;
	*p++ = (n >> 8) & 0xFF;
	*p++ = n & 0xFF;
}

int buff2int(const char *buff)
{
	return  (((unsigned char)(*buff)) << 24) | \
		(((unsigned char)(*(buff+1))) << 16) |  \
		(((unsigned char)(*(buff+2))) << 8) | \
		((unsigned char)(*(buff+3)));
}

void long2buff(int64_t n, char *buff)
{
	unsigned char *p;
	p = (unsigned char *)buff;
	*p++ = (n >> 56) & 0xFF;
	*p++ = (n >> 48) & 0xFF;
	*p++ = (n >> 40) & 0xFF;
	*p++ = (n >> 32) & 0xFF;
	*p++ = (n >> 24) & 0xFF;
	*p++ = (n >> 16) & 0xFF;
	*p++ = (n >> 8) & 0xFF;
	*p++ = n & 0xFF;
}

int64_t buff2long(const char *buff)
{
	unsigned char *p;
	p = (unsigned char *)buff;
	return  (((int64_t)(*p)) << 56) | \
		(((int64_t)(*(p+1))) << 48) |  \
		(((int64_t)(*(p+2))) << 40) |  \
		(((int64_t)(*(p+3))) << 32) |  \
		(((int64_t)(*(p+4))) << 24) |  \
		(((int64_t)(*(p+5))) << 16) |  \
		(((int64_t)(*(p+6))) << 8) | \
		((int64_t)(*(p+7)));
}


char *trim_left(char *pStr)
{
	char *pTemp;
	char *p;
	char *pEnd;
	int nDestLen;

	//pTemp = malloc(10);
	//free(pTemp);
	pEnd = pStr + strlen(pStr);
	for (p=pStr; p<pEnd; p++)
	{
		if (!(' ' == *p|| '\n' == *p || '\r' == *p || '\t' == *p))
		{
			break;
		}
	}

	if ( p == pStr)
	{
		return pStr;
	}

	nDestLen = (pEnd - p) + 1; //including \0
	pTemp = (char *)malloc(nDestLen);
	if (pTemp == NULL)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"malloc %d bytes fail", __LINE__, nDestLen);
		return p;
	}

	memcpy(pTemp, p, nDestLen);
	memcpy(pStr, pTemp, nDestLen);
	//free(pTemp);
	return pStr;
}

char *trim_right(char *pStr)
{
	int len;
	char *p;
	char *pEnd;

	len = strlen(pStr);
	if (len == 0)
	{
		return pStr;
	}

	pEnd = pStr + len - 1;
	for (p = pEnd;  p>=pStr; p--)
	{
		if (!(' ' == *p || '\n' == *p || '\r' == *p || '\t' == *p))
		{
			break;
		}
	}

	if (p != pEnd)
	{
		*(p+1) = '\0';
	}

	return pStr;
}

char *trim(char *pStr)
{
	trim_right(pStr);
	trim_left(pStr);
	return pStr;
}

void printfBuffString(char* title,const char *s,const int len)
{
	unsigned char *p;
	int i;

	p = (unsigned char*)s;
	printf("%s\n",title);
	for(i = 0;i < len; i++)
	{
		printf("%c",*p);
		p++;
	}
	printf("\n");
}

void printfBuffHex(char* title,const char *s,const int len)
{
	unsigned char *p;
	int i;

	p = (unsigned char*)s;
	printf("%s",title);
	for(i = 0;i < len; i++)
	{
		if((i % 16) == 0)
		{
			printf("\n%04X ",i);
		}
		printf("0x%02X ",*p);
		p++;
	}
	printf("\n");
}


char *formatDateYYYYMMDDHHMISS(const time_t t, char *szDateBuff, const int nSize)
{
	time_t timer = t;
	struct tm *tm = localtime(&timer);

	snprintf(szDateBuff, nSize, "%04d%02d%02d%02d%02d%02d", tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
	return szDateBuff;
}

time_t formatDate4String(char *szDateBuff)
{
	struct tm tm_time;
	time_t tm_t;

	if(szDateBuff == NULL)
	{
		tm_t = time(NULL);
		return tm_t;
	}
	sscanf(szDateBuff,"%4d-%2d-%2d %2d:%2d:%2d",&tm_time.tm_year,&tm_time.tm_mon,&tm_time.tm_mday,&tm_time.tm_hour,&tm_time.tm_min,&tm_time.tm_sec);

	tm_time.tm_year -= 1900;
	tm_time.tm_mon--;
	tm_time.tm_isdst = -1;

	tm_t = mktime(&tm_time);

	return tm_t;
}

char* getSysDatetime(char* tm_buff)
{
	if(tm_buff == NULL)
	  return NULL;

	time_t now;
	struct tm *tm_now;
	
	time(&now);
	tm_now = localtime(&now);

	strftime(tm_buff,256,"%Y-%m-%d %H:%M:%S",tm_now);
	return tm_buff;
}

bool fileExists(const char *filename)
{
	return access(filename, 0) == 0;
}

bool isDir(const char *filename)
{
	struct stat buf;
	if (stat(filename, &buf) != 0)
	{
		return false;
	}

	return S_ISDIR(buf.st_mode);
}

bool isFile(const char *filename)
{
	struct stat buf;
	if (stat(filename, &buf) != 0)
	{
		return false;
	}

	return S_ISREG(buf.st_mode);
}


int getFileContent(const char *filename, char **buff, off_t *file_size)
{
	int fd;

	fd = open(filename, O_RDONLY);
	if (fd < 0)
	{
		*buff = NULL;
		*file_size = 0;
		logger_error("file: "__FILE__", line: %d, " \
					"open file %s fail, " \
					"errno: %d, error info: %s", __LINE__, \
					filename, errno, strerror(errno));
		return errno != 0 ? errno : ENOENT;
	}

	if ((*file_size=lseek(fd, 0, SEEK_END)) < 0)
	{
		*buff = NULL;
		*file_size = 0;
		close(fd);
		logger_error("file: "__FILE__", line: %d, " \
					"lseek file %s fail, " \
					"errno: %d, error info: %s", __LINE__, \
					filename, errno, strerror(errno));
		return errno != 0 ? errno : EIO;
	}

	*buff = (char *)malloc(*file_size + 1);
	if (*buff == NULL)
	{
		*file_size = 0;
		close(fd);

		logger_error("file: "__FILE__", line: %d, " \
					"malloc %d bytes fail", __LINE__, \
					(int)(*file_size + 1));
		return errno != 0 ? errno : ENOMEM;
	}

	if (lseek(fd, 0, SEEK_SET) < 0)
	{
		*buff = NULL;
		*file_size = 0;
		close(fd);
		logger_error("file: "__FILE__", line: %d, " \
					"lseek file %s fail, " \
					"errno: %d, error info: %s", __LINE__, \
					filename, errno, strerror(errno));
		return errno != 0 ? errno : EIO;
	}
	if (read(fd, *buff, *file_size) != *file_size)
	{
		free(*buff);
		*buff = NULL;
		*file_size = 0;
		close(fd);
		logger_error("file: "__FILE__", line: %d, " \
					"read from file %s fail, " \
					"errno: %d, error info: %s", __LINE__, \
					filename, errno, strerror(errno));
		return errno != 0 ? errno : EIO;
	}

	(*buff)[*file_size] = '\0';
	close(fd);

	return 0;
}

void chopPath(char *filePath)
{
	int lastIndex;
	if (*filePath == '\0')
	{
		return;
	}

	lastIndex = strlen(filePath) - 1;
	if (filePath[lastIndex] == '/')
	{
		filePath[lastIndex] = '\0';
	}
}

int splitStr(char *src, const char seperator, char **pCols, const int nMaxCols)
{
	char *p;
	char **pCurrent;
	int count = 0;

	if (nMaxCols <= 0)
	{
		return 0;
	}

	p = src;
	pCurrent = pCols;

	while (true)
	{
		*pCurrent = p;
		pCurrent++;

		count++;
		if (count >= nMaxCols)
		{
			break;
		}

		p = strchr(p, seperator);
		if (p == NULL)
		{
			break;
		}

		*p = '\0';
		p++;
	}

	return count;
}

#if 1
int init_pthread_lock(pthread_mutex_t *pthread_lock)
{
	pthread_mutexattr_t mat;
	int result;

	if ((result=pthread_mutexattr_init(&mat)) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"call pthread_mutexattr_init fail, " \
					"errno: %d, error info: %s", \
					__LINE__, result, strerror(result));
		return result;
	}
	if ((result=pthread_mutexattr_settype(&mat, \
						PTHREAD_MUTEX_ERRORCHECK)) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"call pthread_mutexattr_settype fail, " \
					"errno: %d, error info: %s", \
					__LINE__, result, strerror(result));
		return result;
	}
	if ((result=pthread_mutex_init(pthread_lock, &mat)) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"call pthread_mutex_init fail, " \
					"errno: %d, error info: %s", \
					__LINE__, result, strerror(result));
		return result;
	}
	if ((result=pthread_mutexattr_destroy(&mat)) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"call thread_mutexattr_destroy fail, " \
					"errno: %d, error info: %s", \
					__LINE__, result, strerror(result));
		return result;
	}

	return 0;
}

int init_pthread_attr(pthread_attr_t *pattr, const int stack_size)
{
	size_t old_stack_size;
	size_t new_stack_size;
	int result;

	if ((result=pthread_attr_init(pattr)) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"call pthread_attr_init fail, " \
					"errno: %d, error info: %s", \
					__LINE__, result, strerror(result));
		return result;
	}

	if ((result=pthread_attr_getstacksize(pattr, &old_stack_size)) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"call pthread_attr_getstacksize fail, " \
					"errno: %d, error info: %s", \
					__LINE__, result, strerror(result));
		return result;
	}

	if (stack_size > 0)
	{
		if (old_stack_size != stack_size)
		{
			new_stack_size = stack_size;
		}
		else
		{
			new_stack_size = 0;
		}
	}
	else if (old_stack_size < 1 * 1024 * 1024)
	{
		new_stack_size = 1 * 1024 * 1024;
	}
	else
	{
		new_stack_size = 0;
	}

	if (new_stack_size > 0)
	{
		if ((result=pthread_attr_setstacksize(pattr, \
							new_stack_size)) != 0)
		{
			logger_error("file: "__FILE__", line: %d, " \
						"call pthread_attr_setstacksize fail, " \
						"errno: %d, error info: %s", \
						__LINE__, result, strerror(result));
			return result;
		}
	}

	if ((result=pthread_attr_setdetachstate(pattr, \
						PTHREAD_CREATE_DETACHED)) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"call pthread_attr_setdetachstate fail, " \
					"errno: %d, error info: %s", \
					__LINE__, result, strerror(result));
		return result;
	}

	return 0;
}


int create_work_threads(int *count, void *(*start_func)(void *), \
			void *arg, pthread_t *tids, const int stack_size)
{
	int result;
	pthread_attr_t thread_attr;
	pthread_t *ptid;
	pthread_t *ptid_end;

	if ((result=init_pthread_attr(&thread_attr, stack_size)) != 0)
	{
		return result;
	}

	result = 0;
	ptid_end = tids + (*count);
	for (ptid=tids; ptid < ptid_end; ptid++)
	{
		if ((result = pthread_create(ptid, &thread_attr, \
							start_func, arg)) != 0)
		{
			*count = ptid - tids;
			logger_error("file: "__FILE__", line: %d, " \
						"create thread failed, startup threads: %d, " \
						"errno: %d, error info: %s", \
						__LINE__, *count, \
						result, strerror(result));
			break;
		}
	}

	pthread_attr_destroy(&thread_attr);
	return result;
}

int kill_work_threads(pthread_t *tids, const int count)
{
	int result;
	pthread_t *ptid;
	pthread_t *ptid_end;

	ptid_end = tids + count;
	for (ptid=tids; ptid < ptid_end; ptid++)
	{
		if ((result = pthread_kill(*ptid, SIGINT)) != 0)
		{
			logger_error("file: "__FILE__", line: %d, " \
						"kill thread failed, " \
						"errno: %d, error info: %s", \
						__LINE__, result, strerror(result));
		}
	}

	return 0;
}

#endif

int get_split_str_count(const char *str,const char seperator)
{
	assert(str != NULL);
	int count = 1;
	const char *iter = str;
	while(*iter != '\0')
	{
		if((*iter) == seperator)
			count++;
		iter++;
	}
	return count;
}
