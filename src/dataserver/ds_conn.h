/**
*
*
*
*
*
*
**/

#ifndef _DS_CONN_H_
#define _DS_CONN_H_

#include <event.h>
#include "list.h"
#include "common_define.h"
#include "dataserverd.h"
#include "ds_types.h"
#include "ds_queue.h"
#include "lfs_protocol.h"

#ifdef __cplusplus
extern "C"{
#endif

void conn_item_queue_push(queue_head *cq_head,conn_item *conn_item);
conn_item* conn_item_queue_pop(queue_head *cq_head);
int conn_item_queue_walk(queue_head *cq_head);
void conn_item_queue_destroy(queue_head *cq_head);

conn_item* conn_item_new();
void conn_item_free(conn_item *conn_item);


void conn_queue_push(queue_head *cq_head,conn *c);
conn* conn_queue_pop(queue_head *cq_head);
int conn_queue_walk(queue_head *cq_head);
void conn_queue_destroy(queue_head *cq_head);

int conns_init(void);
void conns_destroy(void);
conn* conn_new(const int sfd,enum conn_states init_state,\
		int default_read_buff_size,int default_write_buff_size,\
		const int event_flags,event_callback event_cb,struct event_base *base);
void conn_close(conn *c);
void conn_free(conn *c);
void conn_clean(conn *c);
void conn_reset(conn *c);
void conn_write_reset(conn *c);

void set_conn_state(conn *c,enum conn_states state);
void set_conn_mstate(conn *c,enum conn_states curr_state,\
		enum conn_states next_state);
//int add_conn_event(conn *c,const int new_ev_flags,event_callback event_cb);
int add_conn_event(conn *c,const int new_ev_flags,event_callback event_cb,struct event_base *base);
int update_conn_event(conn *c,const int new_ev_flags,event_callback event_cb);
int del_conn_event(conn *c);

extern conn **conns;
#ifdef __cplusplus
}
#endif
#endif
