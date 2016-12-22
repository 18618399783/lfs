#ifndef _TIMES_H_
#define _TIMES_H_

#ifdef __cplusplus
extern "C"{
#endif
long long nows();
long long nowms();
long long nowus();
char* timestr(long long sec, char *buf);

#ifdef __cplusplus
}
#endif
#endif
