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

#include "logger.h"
#include "list.h"
#include "ts_func.h"
#include "ts_queue.h"
#include "ts_conn.h"


static void __set_conn_state(conn *c,enum conn_states state);
static void __conn_clean(conn *c);
static void __conn_reset(conn *c);
static int __update_conn_event(conn *c,const int new_ev_flags,void (*event_callback)(const int fd,short which,void *arg));
static int __del_conn_event(conn *c);

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
		for(i = 1; i < CONN_ITEMS_PER_ALLOC - 1; i++)
		{
			memset(&items[i],0,sizeof(conn_item));
			conn_item_queue_push(&freecitems,&items[i]);
		}
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

conn* conn_new(const int sfd,enum conn_states init_state,\
		int default_buff_size,const int event_flags,void (*event_callback)(const int fd,short which,void *arg),struct event_base *base)
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
		c->sfd = sfd;
		c->rbuff = c->rcurr = NULL;
		c->wbuff = c->wcurr = NULL;
		c->rsize = default_buff_size;
		c->wsize = default_buff_size; 
		c->rbytes = c->wbytes = 0;
		c->which = 0;
		c->wused = 0;

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

	c->rcurr = c->rbuff;
	c->wcurr = c->wbuff;
	event_set(&c->event,sfd,event_flags,event_callback,(void*)c);
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
	if(NULL == c)
		return;
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
	if(c)
	{
		assert(c->sfd >= 0 && c->sfd < ctxs.max_fds);
		conns[c->sfd] = NULL;
		if(c->rbuff)
			free(c->rbuff);
		if(c->wbuff)
			free(c->wbuff);
		free(c);
	}
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

void set_conn_state(conn *c,enum conn_states state)
{
	__set_conn_state(c,state);
	return;
}

int update_conn_event(conn *c,const int new_ev_flags,\
		void (*event_callback)(const int fd,short which,void *arg))
{
	return __update_conn_event(c,new_ev_flags,event_callback);
}

int del_conn_event(conn *c)
{
	return __del_conn_event(c);
}

static void __set_conn_state(conn *c,enum conn_states state)
{
	if(NULL == c)
		return;
	if(state != c->state)
		c->state = state;
}

int __update_conn_event(conn *c,const int new_ev_flags, void (*event_callback)(const int fd,short which,void *arg))
{
	if(NULL == c) return LFS_ERROR;
	struct event_base *base = c->event.ev_base;
	if(c->event_flags == new_ev_flags)
		return 0;
	if(event_del(&c->event) == LFS_ERROR) return LFS_ERROR;
	event_set(&c->event,c->sfd,new_ev_flags,event_callback,(void*)c);
	event_base_set(base,&c->event);
	c->event_flags = new_ev_flags;
	if(event_add(&c->event,0) == LFS_ERROR) return LFS_ERROR;
	return LFS_OK;
}

static int __del_conn_event(conn *c)
{
	if(NULL == c) return LFS_ERROR;
	if(event_del(&c->event) == LFS_ERROR) return LFS_ERROR;
	return LFS_OK;
}
static void __conn_clean(conn *c)
{
	c->rbytes = c->rlbytes = 0;
	c->wbytes = c->wused = 0;
	c->rcurr = c->rbuff;
	c->wcurr = c->wbuff;
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
	c->rbytes = c->rlbytes = 0;
	c->wbytes = c->wused = 0;
	c->rcurr = c->rbuff;
	c->wcurr = c->wbuff;
}

