/**
*
*
*
*
*
*
**/

#ifndef _TS_CONN_H_
#define _TS_CONN_H_

#include <event.h>
#include "trackerd.h"

#ifdef __cplusplus
extern "C"{
#endif



conn_item* conn_item_new();
void conn_item_free(conn_item *conn_item);
conn* conn_new(const int sfd,enum conn_states init_state,\
		int default_buff_size,const int event_flags,void (*event_callback)(const int fd,short which,void *arg),struct event_base *base);
void conn_close(conn *c);
void conn_free(conn *c);
void conn_clean(conn *c);
void conn_reset(conn *c);
void set_conn_state(conn *c,enum conn_states state);
int update_conn_event(conn *c,const int new_ev_flags,void (*event_callback)(const int fd,short which,void *arg));
int del_conn_event(conn *c);

#ifdef __cplusplus
}
#endif
#endif
