/**
*
*
*
*
*
**/
#ifndef _TS_QUEUE_H_
#define _TS_QUEUE_H_
#ifdef __cplusplus
extern "C"{
#endif

#include "trackerd.h"


void init_queue(queue_head *cq_head);
int queue_active(queue_head *cq_head);
int queue_walk(queue_head *cq_head);
void conn_item_queue_push(queue_head *cq_head,conn_item *conn_item);
conn_item* conn_item_queue_pop(queue_head *cq_head);

#ifdef __cplusplus
}
#endif
#endif
