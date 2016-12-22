/**
*
*
*
*
*
**/

#ifndef _DS_FUNC_H_
#define _DS_FUNC_H_

#include "dataserverd.h"
#ifdef __cplusplus
extern "C"{
#endif

void settings_init(void);
void ctxs_init(void);
void conf_items_init(void);
void dataserver_destroy(void);
int set_cfg2globalobj(void);
int server_sockets(const char *bind_host,const int port,const int timeout);
void CTXS_LOCK();
void CTXS_UNLOCK();
bool is_local_block(const char *ipaddr);
int ctx_snapshot_batdata_load(void);
int ctx_snapshot_batdata_flush(void);

#ifdef __cplusplus
}
#endif

#endif
