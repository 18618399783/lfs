/**
*
*
*
*
**/

#ifndef _SHARED_FUNC_H_
#define _SHARED_FUNC_H_

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include "common_define.h"

#undef strtok_r
#undef __strtok_r

#ifndef _LIBC
/* Get specification. */
# define __strtok_r strtok_r
# define __rawmemchr strchr
#endif

char* strtok_r (char *s, const char *delim, char **save_ptr);

#ifdef weak_alias
	libc_hidden_def (__strtok_r)
weak_alias (__strtok_r, strtok_r)
#endif


char *toLowercase(char *src);
char *toUppercase(char *src);

char *formatDatetime(const time_t nTime, \
	const char *szDateFormat, \
	char *buff, const int buff_size);
time_t formatDate4String(char *szDateBuff);
char* getSysDatetime(char* tm_buff);

int getCharLen(const char *s);


int daemon_init(bool bCloseFiles);

char *bin2hex(const char *s, const int len, char *szHexBuff);
char *hex2bin(const char *s, char *szBinBuff, int *nDestLen);

void hex2ascii(unsigned char * pHex, unsigned char * pAscii, int nLen);
int ascii2hex(unsigned char * pAscii, unsigned char * pHex, int nLen,int *nDestLen);

void int16_t2buff(const int16_t n,char *buff);
int16_t buff2int16_t(const char *buff);
void int2buff(const int n, char *buff);
int buff2int(const char *buff);
void long2buff(int64_t n, char *buff);
int64_t buff2long(const char *buff);

char *trim_left(char *pStr);
char *trim_right(char *pStr);
char *trim(char *pStr);
void printfBuffString(char* title,const char *s,const int len);
void printfBuffHex(char* title,const char *s,const int len);

void chopPath(char *filePath);
bool fileExists(const char *filename);
bool isDir(const char *filename);
bool isFile(const char *filename);
int getFileContent(const char *filename, char **buff, off_t *file_size);
int splitStr(char *src, const char seperator, char **pCols, const int nMaxCols);

int init_pthread_lock(pthread_mutex_t *pthread_lock);
int init_pthread_attr(pthread_attr_t *pattr, const int stack_size);
int create_work_threads(int *count, void *(*start_func)(void *), \
		void *arg, pthread_t *tids, const int stack_size);
int kill_work_threads(pthread_t *tids, const int count);
int get_split_str_count(const char *str,const char seperator);


#endif
