/**
*
**/

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <pthread.h>
#include "shared_func.h"
#include "logger.h"

int g_log_level = LOG_INFO;
int g_log_fd = STDERR_FILENO;
static pthread_mutex_t log_thread_lock;
static bool log_to_cache = false;
static char log_buff[64 * 1024];
static char *pcurrent_log_buff = log_buff;

static int logger_fsync(const bool bNeedLock);

static int check_and_mk_log_dir(const char *base_path)
{
	char data_path[MAX_PATH_SIZE];

	snprintf(data_path, sizeof(data_path), "%s/logs", base_path);
	if (!fileExists(data_path))
	{
		if (mkdir(data_path, 0755) != 0)
		{
			fprintf(stderr, "mkdir \"%s\" failed, " \
				"errno: %d, error info: %s.\n", \
				data_path, errno, strerror(errno));
			return errno != 0 ? errno : EPERM;
		}
	}

	return 0;
}

int logger_init(const char *base_path, const char *filename_prefix)
{
	int result;
	char logfile[MAX_PATH_SIZE];

	if ((result=check_and_mk_log_dir(base_path)) != 0)
	{
		return result;
	}

	logger_destroy();

	if ((result=init_pthread_lock(&log_thread_lock)) != 0)
	{
		return result;
	}

	snprintf(logfile, MAX_PATH_SIZE, "%s/logs/%s.log", \
		base_path, filename_prefix);

	if ((g_log_fd = open(logfile, O_WRONLY | O_CREAT | O_APPEND, 0644)) < 0)
	{
		fprintf(stderr, "open log file \"%s\" to write failed, " \
			"errno: %d, error info: %s.\n", \
			logfile, errno, strerror(errno));
		g_log_fd = STDERR_FILENO;
		result = errno != 0 ? errno : EACCES;
	}
	else
	{
		result = 0;
	}

	return result;
}

void logger_set_cache(const bool bLogCache)
{
	log_to_cache = bLogCache;
}

void logger_destroy()
{
	if (g_log_fd >= 0 && g_log_fd != STDERR_FILENO)
	{
		logger_fsync(true);

		close(g_log_fd);
		g_log_fd = STDERR_FILENO;

		pthread_mutex_destroy(&log_thread_lock);
	}
}

int logger_sync_func(void *args)
{
	return logger_fsync(true);
}

static int logger_fsync(const bool bNeedLock)
{
	int result;
	int write_bytes;

	write_bytes = pcurrent_log_buff - log_buff;
	if (write_bytes == 0)
	{
		return 0;
	}

	result = 0;
	if (bNeedLock && (result=pthread_mutex_lock(&log_thread_lock)) != 0)
	{
		fprintf(stderr, "file: "__FILE__", line: %d, " \
			"call pthread_mutex_lock failed, " \
			"errno: %d, error info: %s.\n", \
			__LINE__, result, strerror(result));
	}

	write_bytes = pcurrent_log_buff - log_buff;
	if (write(g_log_fd, log_buff, write_bytes) != write_bytes)
	{
		result = errno != 0 ? errno : EIO;
		fprintf(stderr, "file: "__FILE__", line: %d, " \
			"call write failed, errno: %d, error info: %s.\n",\
			 __LINE__, result, strerror(result));
	}

	if (g_log_fd != STDERR_FILENO)
	{
		if (fsync(g_log_fd) != 0)
		{
			result = errno != 0 ? errno : EIO;
			fprintf(stderr, "file: "__FILE__", line: %d, " \
				"call fsync failed, errno: %d, error info: %s.\n",\
				 __LINE__, result, strerror(result));
		}
	}

	pcurrent_log_buff = log_buff;
	if (bNeedLock && (result=pthread_mutex_unlock(&log_thread_lock)) != 0)
	{
		fprintf(stderr, "file: "__FILE__", line: %d, " \
			"call pthread_mutex_unlock failed, " \
			"errno: %d, error info: %s.\n", \
			__LINE__, result, strerror(result));
	}

	return result;
}

static void doLog(const char *caption, const char* text, const int text_len, \
		const bool bNeedSync)
{
	time_t t;
	struct tm tm;
	int buff_len;
	int result;

	t = time(NULL);
	localtime_r(&t, &tm);
	if ((result=pthread_mutex_lock(&log_thread_lock)) != 0)
	{
		fprintf(stderr, "file: "__FILE__", line: %d, " \
			"call pthread_mutex_lock failed, " \
			"errno: %d, error info: %s.\n", \
			__LINE__, result, strerror(result));
	}

	if (text_len + 64 > sizeof(log_buff))
	{
		fprintf(stderr, "file: "__FILE__", line: %d, " \
			"log buff size: %d < log text length: %d \n", \
			__LINE__, (int)sizeof(log_buff), text_len + 64);
		pthread_mutex_unlock(&log_thread_lock);
		return;
	}

	if ((pcurrent_log_buff - log_buff) + text_len + 64 > sizeof(log_buff))
	{
		logger_fsync(false);
	}

	buff_len = sprintf(pcurrent_log_buff, \
			"[%04d-%02d-%02d %02d:%02d:%02d,%u,%u] %s - ", \
			tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, \
			tm.tm_hour, tm.tm_min, tm.tm_sec,(unsigned int)getpid(),(unsigned int)pthread_self(),caption);
	pcurrent_log_buff += buff_len;
	memcpy(pcurrent_log_buff, text, text_len);
	pcurrent_log_buff += text_len;
	*pcurrent_log_buff++ = '\n';

	if (!log_to_cache || bNeedSync)
	{
		logger_fsync(false);
	}

	if ((result=pthread_mutex_unlock(&log_thread_lock)) != 0)
	{
		fprintf(stderr, "file: "__FILE__", line: %d, " \
			"call pthread_mutex_unlock failed, " \
			"errno: %d, error info: %s.\n", \
			__LINE__, result, strerror(result));
	}
}

void logger_string(const char *caption,char *title,const unsigned char *s,const int len)
{
	time_t t;
	struct tm tm;
	int buff_len;
	int result;
	int write_bytes;
	char title_buff[1024] = {0};
	unsigned char s_buff[16] = {0};
	int title_len;
	int n;
	int s_buff_len;

	t = time(NULL);
	localtime_r(&t, &tm);
	result = 0;
	n = 0;
	title_len = 0;
	s_buff_len = 0;
	title_len = snprintf(title_buff,1024,"%s",title);
	if ((result = pthread_mutex_lock(&log_thread_lock)) != 0)
	{
		fprintf(stderr, "file: "__FILE__", line: %d, " \
			"call pthread_mutex_lock failed, " \
			"errno: %d, error info: %s.\n", \
			__LINE__, result, strerror(result));
		return;
	}

	if (title_len + 64 > sizeof(log_buff))
	{
		fprintf(stderr, "file: "__FILE__", line: %d, " \
			"log buff size: %d < log text length: %d \n", \
			__LINE__, (int)sizeof(log_buff), title_len + 64);
		pthread_mutex_unlock(&log_thread_lock);
		return;
	}

	buff_len = sprintf(pcurrent_log_buff, \
			"[%04d-%02d-%02d %02d:%02d:%02d,%u,%u] %s - ", \
			tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, \
			tm.tm_hour, tm.tm_min, tm.tm_sec,(unsigned int)getpid(),(unsigned int)pthread_self(), caption);
	pcurrent_log_buff += buff_len;
	memcpy(pcurrent_log_buff, title_buff, title_len);
	pcurrent_log_buff += title_len;
	*pcurrent_log_buff++ = '\n';

	write_bytes = pcurrent_log_buff - log_buff;
	if (write(g_log_fd, log_buff, write_bytes) != write_bytes)
	{
		result = errno != 0 ? errno : EIO;
		fprintf(stderr, "file: "__FILE__", line: %d, " \
			"call write failed, errno: %d, error info: %s.\n",\
			 __LINE__, result, strerror(result));
		return;
	}
	for(;n < len; n++)
	{
		memset(s_buff,0,16);
		s_buff_len = 0;
		s_buff_len = snprintf((char*)s_buff,16,"%c",s[n]);
		write_bytes = 0;
		write_bytes = write(g_log_fd,s_buff,s_buff_len);
	}
	write_bytes = write(g_log_fd,"\n",1);

	if (g_log_fd != STDERR_FILENO)
	{
		if (fsync(g_log_fd) != 0)
		{
			result = errno != 0 ? errno : EIO;
			fprintf(stderr, "file: "__FILE__", line: %d, " \
				"call fsync failed, errno: %d, error info: %s.\n",\
				 __LINE__, result, strerror(result));
		}
	}

	pcurrent_log_buff = log_buff;
	if ((result = pthread_mutex_unlock(&log_thread_lock)) != 0)
	{
		fprintf(stderr, "file: "__FILE__", line: %d, " \
			"call pthread_mutex_unlock failed, " \
			"errno: %d, error info: %s.\n", \
			__LINE__, result, strerror(result));
	}
	return;
}

void logger_hex(const char *caption,char *title,const unsigned char *s,const int len)
{
	time_t t;
	struct tm tm;
	int buff_len;
	int result;
	int write_bytes;
	char title_buff[1024] = {0};
	unsigned char s_buff[16] = {0};
	int title_len;
	int n;
	int s_buff_len;

	t = time(NULL);
	localtime_r(&t, &tm);
	result = 0;
	n = 0;
	title_len = 0;
	s_buff_len = 0;
	title_len = snprintf(title_buff,1024,"%s",title);
	if ((result = pthread_mutex_lock(&log_thread_lock)) != 0)
	{
		fprintf(stderr, "file: "__FILE__", line: %d, " \
			"call pthread_mutex_lock failed, " \
			"errno: %d, error info: %s.\n", \
			__LINE__, result, strerror(result));
		return;
	}

	if (title_len + 64 > sizeof(log_buff))
	{
		fprintf(stderr, "file: "__FILE__", line: %d, " \
			"log buff size: %d < log text length: %d \n", \
			__LINE__, (int)sizeof(log_buff), title_len + 64);
		pthread_mutex_unlock(&log_thread_lock);
		return;
	}

	buff_len = sprintf(pcurrent_log_buff, \
			"[%04d-%02d-%02d %02d:%02d:%02d,%u,%u] %s - ", \
			tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, \
			tm.tm_hour, tm.tm_min, tm.tm_sec,(unsigned int)getpid(),(unsigned int)pthread_self(), caption);
	pcurrent_log_buff += buff_len;
	memcpy(pcurrent_log_buff, title_buff, title_len);
	pcurrent_log_buff += title_len;
	//*pcurrent_log_buff++ = '\n';

	write_bytes = pcurrent_log_buff - log_buff;
	if (write(g_log_fd, log_buff, write_bytes) != write_bytes)
	{
		result = errno != 0 ? errno : EIO;
		fprintf(stderr, "file: "__FILE__", line: %d, " \
			"call write failed, errno: %d, error info: %s.\n",\
			 __LINE__, result, strerror(result));
		return;
	}
	for(;n < len; n++)
	{
		if((n % 16) == 0)
		{
			memset(s_buff,0,16);
			s_buff_len = 0;
			s_buff_len = snprintf((char*)s_buff,16,"\n0x%04X ",n);
			write_bytes = 0;
			write_bytes = write(g_log_fd,s_buff,s_buff_len);
		}
		memset(s_buff,0,16);
		s_buff_len = 0;
		s_buff_len = snprintf((char*)s_buff,16,"0x%02X ",s[n]);
		write_bytes = 0;
		write_bytes = write(g_log_fd,s_buff,s_buff_len);
	}
	write_bytes = write(g_log_fd,"\n",1);

	if (g_log_fd != STDERR_FILENO)
	{
		if (fsync(g_log_fd) != 0)
		{
			result = errno != 0 ? errno : EIO;
			fprintf(stderr, "file: "__FILE__", line: %d, " \
				"call fsync failed, errno: %d, error info: %s.\n",\
				 __LINE__, result, strerror(result));
		}
	}

	pcurrent_log_buff = log_buff;
	if ((result = pthread_mutex_unlock(&log_thread_lock)) != 0)
	{
		fprintf(stderr, "file: "__FILE__", line: %d, " \
			"call pthread_mutex_unlock failed, " \
			"errno: %d, error info: %s.\n", \
			__LINE__, result, strerror(result));
	}
	return;
}

void logger_it(const int priority, const char* format, ...)
{
	bool bNeedSync;
	char text[LINE_MAX];
	char *caption;
	int len;

	va_list ap;
	va_start(ap, format);
	len = vsnprintf(text, sizeof(text), format, ap);
	va_end(ap);

	switch(priority)
	{
		case LOG_DEBUG:
			bNeedSync = true;
			caption = "DEBUG";
			break;
		case LOG_INFO:
			bNeedSync = true;
			caption = "INFO";
			break;
		case LOG_NOTICE:
			bNeedSync = false;
			caption = "NOTICE";
			break;
		case LOG_WARNING:
			bNeedSync = false;
			caption = "WARNING";
			break;
		case LOG_ERR:
			bNeedSync = false;
			caption = "ERROR";
			break;
		case LOG_CRIT:
			bNeedSync = true;
			caption = "CRIT";
			break;
		case LOG_ALERT:
			bNeedSync = true;
			caption = "ALERT";
			break;
		case LOG_EMERG:
			bNeedSync = true;
			caption = "EMERG";
			break;
		default:
			bNeedSync = false;
			caption = "UNKOWN";
			break;
	}

	doLog(caption, text, len, bNeedSync);
}

void set_logger_level(char *pLogLevel)
{
	if (pLogLevel != NULL)
	{
		toUppercase(pLogLevel);
		if ( strncmp(pLogLevel, "DEBUG", 5) == 0 || \
					strcmp(pLogLevel, "LOG_DEBUG") == 0)
		{
			g_log_level = LOG_DEBUG;
		}
		else if ( strncmp(pLogLevel, "INFO", 4) == 0 || \
					strcmp(pLogLevel, "LOG_INFO") == 0)
		{
			g_log_level = LOG_INFO;
		}
		else if ( strncmp(pLogLevel, "NOTICE", 6) == 0 || \
					strcmp(pLogLevel, "LOG_NOTICE") == 0)
		{
			g_log_level = LOG_NOTICE;
		}
		else if ( strncmp(pLogLevel, "WARN", 4) == 0 || \
					strcmp(pLogLevel, "LOG_WARNING") == 0)
		{
			g_log_level = LOG_WARNING;
		}
		else if ( strncmp(pLogLevel, "ERR", 3) == 0 || \
					strcmp(pLogLevel, "LOG_ERR") == 0)
		{
			g_log_level = LOG_ERR;
		}
		else if ( strncmp(pLogLevel, "CRIT", 4) == 0 || \
					strcmp(pLogLevel, "LOG_CRIT") == 0)
		{
			g_log_level = LOG_CRIT;
		}
		else if ( strncmp(pLogLevel, "ALERT", 5) == 0 || \
					strcmp(pLogLevel, "LOG_ALERT") == 0)
		{
			g_log_level = LOG_ALERT;
		}
		else if ( strncmp(pLogLevel, "EMERG", 5) == 0 || \
					strcmp(pLogLevel, "LOG_EMERG") == 0)
		{
			g_log_level = LOG_EMERG;
		}
	}
}

#ifndef LOG_FORMAT_CHECK

#define _DO_LOG(priority, caption, bNeedSync) \
	char text[LINE_MAX]; \
	int len; \
\
	if (g_log_level < priority) \
	{ \
		return; \
	} \
\
	{ \
	va_list ap; \
	va_start(ap, format); \
	len = vsnprintf(text, sizeof(text), format, ap);  \
	va_end(ap); \
	} \
\
	doLog(caption, text, len, bNeedSync); \


void logger_emerg(const char* format, ...)
{
	_DO_LOG(LOG_EMERG, "EMERG", true)
}

void logger_alert(const char* format, ...)
{
	_DO_LOG(LOG_ALERT, "ALERT", true)
}

void logger_crit(const char* format, ...)
{
	_DO_LOG(LOG_CRIT, "CRIT", true)
}

void logger_error(const char *format, ...)
{
	_DO_LOG(LOG_ERR, "ERROR", false)
}

void logger_warning(const char *format, ...)
{
	_DO_LOG(LOG_WARNING, "WARNING", false)
}

void logger_notice(const char* format, ...)
{
	_DO_LOG(LOG_NOTICE, "NOTICE", false)
}

void logger_info(const char *format, ...)
{
	_DO_LOG(LOG_INFO, "INFO", true)
}

void logger_debug(const char* format, ...)
{
	_DO_LOG(LOG_DEBUG, "DEBUG", true)
}

#endif

