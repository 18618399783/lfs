/**
*
*
*
*
*
*
*
**/
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include <event.h>
#include <pthread.h>

#include "logger.h"
#include "list.h"
#include "ds_types.h"
#include "ds_func.h"
#include "ds_queue.h"
#include "ds_file.h"
#include "ds_conn.h"

conn **conns;

static inline void __conn_item_push(queue_head *cq_head,conn_item *conn_item);
static inline conn_item* __conn_item_pop(queue_head *cq_head);
static void __set_conn_state(conn *c,enum conn_states state);
static void __conn_clean(conn *c);
static void __conn_reset(conn *c);
static int __update_conn_event(conn *c,const int new_ev_flags,event_callback event_cb);

void conn_item_queue_push(queue_head *cq_head,conn_item *conn_item)
{
	pthread_mutex_lock(&(cq_head->lock));
	__conn_item_push(cq_head,conn_item);
	pthread_mutex_unlock(&(cq_head->lock));
}

conn_item* conn_item_queue_pop(queue_head *cq_head)
{
	return __conn_item_pop(cq_head);
}

int conn_item_queue_walk(queue_head *cq_head)
{
	int count = 0;
	struct list_head *plist;
	if(!queue_active(cq_head))
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

void conn_item_queue_destroy(queue_head *cq_head)
{
	struct list_head *plist;
	if(!queue_active(cq_head))
		return;
	pthread_mutex_lock(&(cq_head->lock));
	list_for_each(plist,&(cq_head->head))
	{
		void *obj = (void*)list_entry(plist,struct conn_item_st,list);
		if(obj)
		{
			free(obj);
			obj = NULL;
		}
	}
	pthread_mutex_unlock(&(cq_head->lock));
	return;
}

conn_item* conn_item_new()
{
	conn_item *item = NULL;
	
	item = conn_item_queue_pop(&freecitems); 
	if(!item)
	{
		int i = 0;
		conn_item *items = NULL;
		items = malloc(sizeof(conn_item) * CONN_ITEMS_PER_ALLOC);
		if(!items)
		{
			logger_error("file: "__FILE__", line: %d, " \
						"Failed to allocate for connection pool!", __LINE__);
			return NULL;
		}
		item = &items[0];
		pthread_mutex_lock(&(freecitems.lock));
		for(i = 1; i < CONN_ITEMS_PER_ALLOC - 1; i++)
		{
			memset(&items[i],0,sizeof(conn_item));
			__conn_item_push(&freecitems,&items[i]);
		}
		pthread_mutex_unlock(&(freecitems.lock));
	}
	memset(item,0,sizeof(conn_item));
	return item;
}

void conn_item_free(conn_item *item)
{
	if(NULL == item)
		return;
	memset(item,0,sizeof(conn_item));
	conn_item_queue_push(&freecitems,item);
}

void conn_queue_push(queue_head *cq_head,conn *c)
{
	pthread_mutex_lock(&(cq_head->lock));
	list_add_tail(&(c->list),&(cq_head->head));
	pthread_mutex_unlock(&(cq_head->lock));
	return;
}

conn* conn_queue_pop(queue_head *cq_head)
{
	conn *c = NULL;
	struct list_head *plist;

	if(!queue_active(cq_head))
		return NULL;
	pthread_mutex_lock(&(cq_head->lock));
	list_for_each(plist,&(cq_head->head))
	{
		c = list_entry(plist,conn,list);
		if(c)
			break;
	}
	if(c)
		list_del_init(&(c->list));
	pthread_mutex_unlock(&(cq_head->lock));
	return c;
}

int conn_queue_walk(queue_head *cq_head)
{
	int count = 0;
	struct list_head *plist;
	if(!queue_active(cq_head))
		return 0;
	pthread_mutex_lock(&(cq_head->lock));
	list_for_each(plist,&(cq_head->head))
	{
		void *obj = (void*)list_entry(plist,conn,list);
		if(obj)
			count++;
	}
	pthread_mutex_unlock(&(cq_head->lock));
	return count;
}

void conn_queue_destroy(queue_head *cq_head)
{
	struct list_head *plist;
	if(!queue_active(cq_head))
		return;
	pthread_mutex_lock(&(cq_head->lock));
	list_for_each(plist,&(cq_head->head))
	{
		void *obj = (void*)list_entry(plist,conn,list);
		if(obj)
		{
			free(obj);
			obj = NULL;
		}
	}
	pthread_mutex_unlock(&(cq_head->lock));
	return;
}

int conns_init(void)
{
	init_queue(&freecitems);
	
	if((conns = calloc(ctxs.max_fds,sizeof(conn*))) == NULL)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Failed to allocate connection pool!", __LINE__);
		return -1;
	}
	return 0;
}

void conns_destroy(void)
{
	assert(conns != NULL);
	int i;
	for(i = 0; i < ctxs.max_fds; i++)
	{
		if(conns[i])
		{
			free(conns[i]);
			conns[i] = NULL;
		}
	}
	free(conns);
	conns = NULL;
}

conn* conn_new(const int sfd,enum conn_states init_state,\
		int default_read_buff_size,int default_write_buff_size,\
		const int event_flags,event_callback event_cb,struct event_base *base)
{
	conn *c = NULL;
	assert(sfd >= 0 && sfd < ctxs.max_fds);
	c = conns[sfd];
	if(NULL == c)
	{
		c = (conn*)malloc(sizeof(conn));
		if(!c)
		{
			logger_error("file: "__FILE__", line: %d, " \
						"Failed to allocate connection object!", __LINE__);
			return NULL;
		}
		memset(c,0,sizeof(conn));
		c->sfd = sfd;
		c->rbuff = c->rcurr = NULL;
		c->wbuff = c->wcurr = NULL;
		c->fctx = NULL;
		c->rsize = default_read_buff_size;
		c->wsize = default_write_buff_size; 
		c->rbytes = c->wbytes = 0;
		c->which = 0;
		c->wused = 0;
		c->alloc_count = 0;

		c->rbuff = (char*)malloc(c->rsize);
		c->wbuff = (char*)malloc(c->wsize);	

		if(c->rbuff == NULL || c->wbuff == NULL)
		{
			conn_free(c);
			return NULL;
		}
		c->rcurr = c->rbuff;
		c->wcurr = c->wbuff;
		conns[sfd] = c;
	}
	c->state = init_state;
	c->rbytes = c->wbytes = 0;
	c->which = 0;
	c->wused = 0;
	c->alloc_count = 0;

	c->fctx = NULL;
	c->rcurr = c->rbuff;
	c->wcurr = c->wbuff;
	event_set(&c->event,sfd,event_flags,event_cb,(void*)c);
	event_base_set(base,&c->event);
	c->event_flags = event_flags;
	
	if(event_add(&c->event,0) == -1)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"Failed to add event!", __LINE__);
		return NULL;
	}
	return c;
}

void conn_close(conn *c)
{
	assert(c != NULL);
	event_del(&c->event);
	__conn_clean(c);
	__set_conn_state(c,conn_closed);
	close(c->sfd);
	CTXS_LOCK();
	ctxs.curr_conns--;
	CTXS_UNLOCK();
}

void conn_free(conn *c)
{
	assert(c != NULL);
	if(c->rbuff)
		free(c->rbuff);
	if(c->wbuff)
		free(c->wbuff);
	if(c->fctx)
	{
		file_ctx_clean(c->fctx);
		c->fctx = NULL;
	}
	free(c);
}

void conn_clean(conn *c)
{
	assert(c != NULL);
	return __conn_clean(c);
}

void conn_reset(conn *c)
{
	assert(c != NULL);
	return __conn_reset(c);
}

void conn_write_reset(conn *c)
{
	assert(c != NULL);
	if(c->wbuff)
	{
		memset(c->wbuff,0,c->wsize);
	}
	c->wbytes = c->wused = 0;
	c->wcurr = c->wbuff;
}

void set_conn_state(conn *c,enum conn_states state)
{
	assert(c != NULL);
	__set_conn_state(c,state);
	return;
}

void set_conn_mstate(conn *c,enum conn_states curr_state,\
		enum conn_states next_state)
{
	assert(c != NULL);
	if(curr_state != c->state)
		c->state = curr_state;
	if(next_state != c->nextto)
		c->nextto = next_state;
	return;
}

int update_conn_event(conn *c,const int new_ev_flags,event_callback event_cb)
{
	return __update_conn_event(c,new_ev_flags,event_cb);
}

int del_conn_event(conn *c)
{
	assert(c != NULL);
	if(event_del(&c->event) == -1) return -1;
	return 0;
}

static inline void __conn_item_push(queue_head *cq_head,conn_item *conn_item)
{
	list_add_tail(&(conn_item->list),&(cq_head->head));
}

static inline conn_item* __conn_item_pop(queue_head *cq_head)
{
	struct conn_item_st *c = NULL;
	struct list_head *plist;

	if(!queue_active(cq_head))
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
static void __set_conn_state(conn *c,enum conn_states state)
{
	if(state != c->state)
		c->state = state;
}

int add_conn_event(conn *c,const int new_ev_flags,event_callback event_cb,struct event_base *base)
{
	assert(c != NULL);
	event_set(&c->event,c->sfd,new_ev_flags,event_cb,(void*)c);
	event_base_set(base,&c->event);
	c->event_flags = new_ev_flags;
	
	if(event_add(&c->event,0) == -1)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"Failed to add event!", __LINE__);
		return -1;
	}
	return 0;
}

int __update_conn_event(conn *c,const int new_ev_flags, event_callback event_cb)
{
	assert(c != NULL);
	struct event_base *base = c->event.ev_base;
	if(c->event_flags == new_ev_flags)
		return 0;
	if(event_del(&c->event) == -1) return -1;
	event_set(&c->event,c->sfd,new_ev_flags,event_cb,(void*)c);
	event_base_set(base,&c->event);
	c->event_flags = new_ev_flags;
	if(event_add(&c->event,0) == -1) return -1;
	return 0;
}

static void __conn_clean(conn *c)
{
	c->rbytes = 0;
	c->wbytes = c->wused = 0;
	c->alloc_count = 0;
	c->rcurr = c->rbuff;
	c->wcurr = c->wbuff;
	if(c->fctx)
	{
		file_ctx_clean(c->fctx);
		c->fctx = NULL;
	}
}

static void __conn_reset(conn *c)
{
	if(c->rbuff)
	{
		memset(c->rbuff,0,c->rsize);
	}
	if(c->wbuff)
	{
		memset(c->wbuff,0,c->wsize);
	}
	c->rbytes = 0;
	c->wbytes = c->wused = 0;
	c->alloc_count = 0;
	c->rcurr = c->rbuff;
	c->wcurr = c->wbuff;
	if(c->fctx)
	{
		file_ctx_buff_reset(c->fctx);
	}
}

