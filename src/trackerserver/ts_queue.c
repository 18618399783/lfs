/**
*
*
*
*
*
**/

#include <stdlib.h>
#include <string.h>

#include "logger.h"
#include "list.h"
#include "ts_queue.h"


static inline void __init_queue(queue_head *cq_head);
static inline int __queue_active(queue_head *cq_head);
static inline int __queue_walk(queue_head *cq_head);
static inline void __conn_item_push(queue_head *cq_head,conn_item *conn_item);
static inline conn_item* __conn_item_pop(queue_head *cq_head);

void init_queue(queue_head *cq_head)
{
	__init_queue(cq_head);
}

int queue_active(queue_head *cq_head)
{
	return __queue_active(cq_head);
}

int queue_walk(queue_head *cq_head)
{
	return __queue_walk(cq_head);
}

void conn_item_queue_push(queue_head *cq_head,conn_item *conn_item)
{
	__conn_item_push(cq_head,conn_item);
}

conn_item* conn_item_queue_pop(queue_head *cq_head)
{
	return __conn_item_pop(cq_head);
}


static inline void __init_queue(queue_head *cq_head)
{
	pthread_mutex_init(&(cq_head->lock),NULL);
	INIT_LIST_HEAD(&(cq_head->head));
}

static inline int __queue_active(queue_head *cq_head)
{
	return !list_empty(&(cq_head->head));
}

static inline int __queue_walk(queue_head *cq_head)
{
	int count = 0;
	struct list_head *plist;
	if(!__queue_active(cq_head))
		return 0;
	pthread_mutex_lock(&(cq_head->lock));
	list_for_each(plist,&(cq_head->head))
	{
		void *obj = (void*)list_entry(plist,struct conn_item_st,list);
		if(obj)
			count++;
	}
	pthread_mutex_unlock(&(cq_head->lock));
	return count;
}

static inline void __conn_item_push(queue_head *cq_head,conn_item *conn_item)
{
	pthread_mutex_lock(&(cq_head->lock));
	list_add_tail(&(conn_item->list),&(cq_head->head));
	pthread_mutex_unlock(&(cq_head->lock));
}

static inline conn_item* __conn_item_pop(queue_head *cq_head)
{
	struct conn_item_st *c = NULL;
	struct list_head *plist;

	if(!__queue_active(cq_head))
		return NULL;
	pthread_mutex_lock(&(cq_head->lock));
	list_for_each(plist,&(cq_head->head))
	{
		c = list_entry(plist,struct conn_item_st,list);
		if(c)
			break;
	}
	if(c)
		list_del_init(&(c->list));
	pthread_mutex_unlock(&(cq_head->lock));
	return c;
}
