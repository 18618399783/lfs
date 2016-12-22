#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <stdio.h>
#include "times.h"

long long nowus()
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return ((unsigned long long)tv.tv_sec * 1000 * 1000 + tv.tv_usec);
}

long long nowms()
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return ((unsigned long long)tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

long long nows()
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return ((unsigned long long)tv.tv_sec + tv.tv_usec / 1000000);
}

char* timestr(long long sec, char *buf)
{
    time_t now;
    struct tm tm;

    time(&now);
    localtime_r(&now, &tm);
    sprintf(buf, "%04d-%02d-%02d %02d:%02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

