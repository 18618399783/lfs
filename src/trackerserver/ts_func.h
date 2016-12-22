/**
*
*
*
*
*
**/

#ifndef _TS_FUNC_H_
#define _TS_FUNC_H_

#include "trackerd.h"
#ifdef __cplusplus
extern "C"{
#endif

void settings_init(void);
void ctxs_init(void);
void conf_items_init(void);
void conns_init(void);
int server_sockets(const char *bind_host,const int port,const int timeout);
void tracker_destroy();
int set_cfg2globalobj();
void CTXS_LOCK();
void CTXS_UNLOCK();
void MEM_FILE_LOCK();
void MEM_FILE_UNLOCK();

#ifdef __cplusplus
}
#endif

#endif
