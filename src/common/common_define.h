#ifndef _COMMON_DEFINE_H_
#define _COMMON_DEFINE_H_
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <stdint.h>

#define _DEBUG_

#define OS_BITS  64
#define IP_ADDRESS_SIZE 16
#define MAX_PATH_SIZE         256
#define LOG_FILE_DIR          "lfs"
#define CONF_FILE_DIR         "conf"
#define DEFAULT_MAX_CONNECTONS 256
#define LFS_BASE64_BUFF_SIZE 256


#ifdef OS_BITS
  #if OS_BITS == 64
    #define INT64_PRINTF_FORMAT   "%ld"
  #else
    #define INT64_PRINTF_FORMAT   "%lld"
  #endif
#else
  #define INT64_PRINTF_FORMAT   "%lld"
#endif

#define MAKEWORD(a,b) ((WORD)(((BYTE)(a)) | ((WORD)((BYTE)(b))) << 8))
#define STRERROR(no) (strerror(no) != NULL ? strerror(no) : "Unkown error")

#define setbit(x,y) x|=(1<<y) //将x的第y位置1
#define clrbit(x,y) x&=~(1<<y) //将x的第y位清0

#define LFS_OK 0
#define LFS_ERROR -1

#ifndef __cplusplus
#ifndef true
typedef char bool;
#define true 1
#define false 0
#endif
#endif




#endif
