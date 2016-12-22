/**
 *
**/

//logger.h
#ifndef _LOGGER_H_
#define _LOGGER_H_

#include <syslog.h>
#include "common_define.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int g_log_level;
extern int g_log_fd;

int logger_init(const char *base_path, const char *filename_prefix);
void logger_destroy();

void logger_string(const char *caption,char *title,const unsigned char *s,const int len);
void logger_hex(const char *caption,char *title,const unsigned char *s,const int len);
void logger_it(const int priority, const char* format, ...);
int logger_sync_func(void *args);
void logger_set_cache(const bool bLogCache);


#ifdef LOG_FORMAT_CHECK  
#define logger_emerg   printf
#define logger_crit    printf
#define logger_alert   printf
#define logger_error   printf
#define logger_warning printf
#define logger_notice  printf
#define logger_info    printf
#define logger_debug   printf

#else

void logger_emerg(const char* format, ...);
void logger_crit(const char* format, ...);
void logger_alert(const char* format, ...);
void logger_error(const char* format, ...);
void logger_warning(const char* format, ...);
void logger_notice(const char* format, ...);
void logger_info(const char* format, ...);
void logger_debug(const char* format, ...);

void set_logger_level(char *pLogLevel);

#endif
#ifdef __cplusplus
}
#endif

#endif


