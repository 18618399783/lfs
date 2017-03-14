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
#include <errno.h>
#include <pthread.h>
#include <assert.h>


#include "logger.h"
#include "ts_types.h"
#include "ts_cluster.h"
#include "ts_wlc.h"

#define WLC_SKIPLIST_MAX_LEVEL 16
#define WLC_SKIPLIST_FACTOR 0.25


wlc_skiplist_node* wlcsl_node_new(int level,int wlc,datasevr_block *dblk)
{
	wlc_skiplist_node *wsn = \
							 (wlc_skiplist_node*)malloc(sizeof(*wsn) + level * sizeof(struct wlc_skiplist_level_st));
	wsn->dblk = dblk;
	wsn->wlc = wlc;
	return wsn;
}

wlc_skiplist* wlcsl_create(void)
{
	int j;
	wlc_skiplist *wsl;

	wsl = (wlc_skiplist*)malloc(sizeof(*wsl));
	wsl->level = 1;
	wsl->length = 0;
	wsl->header = wlcsl_node_new(WLC_SKIPLIST_MAX_LEVEL,0,NULL);
	for(j = 0; j < WLC_SKIPLIST_MAX_LEVEL; j++)
	{
		wsl->header->level[j].forward = NULL;
	}
	wsl->header->backward = NULL;
	wsl->tail = NULL;
	return wsl;
}

void wlcsl_node_free(wlc_skiplist_node *node)
{
	assert(node != NULL);
	free(node);
	node = NULL;
}

void wlcsl_free(wlc_skiplist *wlcsl)
{
	assert(wlcsl != NULL);
	wlc_skiplist_node *node = wlcsl->header->level[0].forward, *next;
	
	wlcsl_node_free(wlcsl->header);
	while(node)
	{
		next = node->level[0].forward;
		wlcsl_node_free(node);
		node = next;
	}
	free(wlcsl);
	wlcsl = NULL;
}

int wlcsl_random_level(void)
{
	int level = 1;
	while((random() & 0xFFFF) < (WLC_SKIPLIST_FACTOR * 0xFFFF ))
		level += 1;
	return (level < WLC_SKIPLIST_MAX_LEVEL)?level:WLC_SKIPLIST_MAX_LEVEL;
}

wlc_skiplist_node* wlcsl_insert(wlc_skiplist *wlcsl,int wlc,datasevr_block *dblk)
{
	assert(wlcsl != NULL);
	assert(dblk != NULL);
	wlc_skiplist_node *update[WLC_SKIPLIST_MAX_LEVEL],*x;
	int i,level;

	x = wlcsl->header;
	for(i = wlcsl->level - 1;i >= 0; i--)
	{
		while(x->level[i].forward && \
				(x->level[i].forward->wlc < wlc || \
				 (x->level[i].forward->wlc == wlc && \
				  x->level[i].forward->dblk->block_id < \
				  dblk->block_id)))
		{
			x = x->level[i].forward;
		}
		update[i] = x;
	}
	level = wlcsl_random_level();
	if(level > wlcsl->level)
	{
		for(i = wlcsl->level; i < level; i++)
		{
			update[i] = wlcsl->header;
		}
		wlcsl->level = level;
	}
	x = wlcsl_node_new(level,wlc,dblk);
	for(i = 0; i < level; i++)
	{
		x->level[i].forward = update[i]->level[i].forward;
		update[i]->level[i].forward = x;
	}
	x->backward = (update[0] == wlcsl->header) ? NULL : update[0];
	if(x->level[0].forward)
		x->level[0].forward->backward = x;
	else
		wlcsl->tail = x;
	wlcsl->length++;
	return x;
}

void wlcsl_node_delete(wlc_skiplist *wlcsl,wlc_skiplist_node *x,\
		wlc_skiplist_node **update)
{
	assert(wlcsl != NULL);
	int i;
	
	for(i = 0; i < wlcsl->level; i++)
	{
		if(update[i]->level[i].forward == x)
		{
			update[i]->level[i].forward = x->level[i].forward;
		}
	}
	if(x->level[0].forward)
	{
		x->level[0].forward->backward = x->backward;
	}
	else
	{
		wlcsl->tail = x->backward;
	}
	while(wlcsl->level > 1 && wlcsl->header->level[wlcsl->level - 1].forward == NULL)
	{
		wlcsl->level--;
	}
	wlcsl->length--;
}

int wlcsl_delete(wlc_skiplist *wlcsl,int wlc,datasevr_block *dblk,\
		wlc_skiplist_node **node)
{
	assert(wlcsl != NULL);
	assert(dblk != NULL);
	wlc_skiplist_node *update[WLC_SKIPLIST_MAX_LEVEL], *x;
	int i;

	x = wlcsl->header;
	for(i = wlcsl->level - 1; i >= 0; i--)
	{
		while(x->level[i].forward && \
				(x->level[i].forward->wlc < wlc || \
				 (x->level[i].forward->wlc == wlc && \
				  x->level[i].forward->dblk->block_id != \
				  dblk->block_id)))
		{
			x = x->level[i].forward;
		}
		update[i] = x;
	}
	x = x->level[0].forward;
	if(x && ((x->wlc == wlc) && (x->dblk->block_id == dblk->block_id)))
	{
		wlcsl_node_delete(wlcsl,x,update);
		if(!node)
			wlcsl_node_free(x);
		else
			*node = x;
		return 1;
	}
	return 0;
}

wlc_skiplist_node* wlcsl_find(wlc_skiplist *wlcsl,int wlc,datasevr_block *dblk)
{
	assert(wlcsl != NULL);
	assert(dblk != NULL);
	int i;
	wlc_skiplist_node *x;

	x = wlcsl->header;
	for(i = wlcsl->level - 1; i >= 0; i--)
	{
		while(x->level[i].forward && \
				(x->level[i].forward->wlc < wlc || \
				 (x->level[i].forward->wlc == wlc && \
				  x->level[i].forward->dblk->block_id != dblk->block_id)))
		{
			x = x->level[i].forward;
		}
	}
	x = x->level[0].forward;
	if(x && ((wlc == x->wlc) && (x->dblk->block_id == dblk->block_id)))
	{
		return x;
	}
	return NULL;
}

int cluster_wlc_block_add(wlc_skiplist *wlcsl,wlc_ctx *wctx)
{
	assert(wlcsl != NULL);
	assert(wctx != NULL);
	wlc_skiplist_node *node = NULL,*x;
	
	if(wctx->old_wlc == wctx->new_wlc)
		return LFS_OK;
	node = wlcsl_find(wlcsl,wctx->old_wlc,wctx->dblk);
	if(node)
	{
		wlcsl_delete(wlcsl,node->wlc,node->dblk,NULL);
	}
	x = wlcsl_insert(wlcsl,wctx->new_wlc,wctx->dblk);	
	if(!x)
		return LFS_ERROR;
	return LFS_OK;
}

datasevr_block* cluster_wlc_write_block_get(wlc_skiplist *wlcsl)
{
	assert(wlcsl != NULL);
	datasevr_block *dblk = NULL;
	wlc_skiplist_node *x;
	
	x = wlcsl->header; 
	while(x->level[0].forward)
	{
		x = x->level[0].forward;
		if(x)
		{
			time_t curr_time = time(NULL);
			if((curr_time - x->dblk->last_heartbeat_time) > \
					confitems.heart_beat_interval)
			{
				x->dblk->state = off_line;
				wlcsl_delete(wlcsl,x->wlc,x->dblk,NULL);
				continue;
			}
			else
			{
				dblk = x->dblk;
				break;
			}
		}
	}
	return dblk;
}

