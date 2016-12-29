/**
*
*
*
*
*
**/
#ifndef _TS_WLC_H_
#define _TS_WLC_H_

#include "trackerd.h"
#include "ts_types.h"

#ifdef __cplusplus
extern "C"{
#endif

typedef struct wlc_skiplist_node_st{
	struct datasevr_block_st *dblk;
	int wlc;
	struct wlc_skiplist_node_st *backward;
	struct wlc_skiplist_level_st{
		struct wlc_skiplist_node_st *forward;
		unsigned int span;
	}level[];
}wlc_skiplist_node;

typedef struct wlc_skiplist_st{
	struct wlc_skiplist_node_st *header,*tail;
	unsigned long length;
	int level;
}wlc_skiplist;

typedef struct wlc_ctx_st{
	struct datasevr_block_st *dblk;
	int old_wlc;
	int new_wlc;
}wlc_ctx;

wlc_skiplist_node* wlcsl_node_new(int level,int wlc,struct datasevr_block_st *dblk);
wlc_skiplist* wlcsl_create(void);
void wlcsl_node_free(wlc_skiplist_node *node);
void wlcsl_free(wlc_skiplist *wlcsl);
int wlcsl_random_level(void);
wlc_skiplist_node* wlcsl_insert(wlc_skiplist *wlcsl,int wlc,struct datasevr_block_st *dblk);
void wlcsl_node_delete(wlc_skiplist *wlcsl,wlc_skiplist_node *x,\
		wlc_skiplist_node **update);
int wlcsl_delete(wlc_skiplist *wlcsl,int wlc,struct datasevr_block_st *dblk,\
		wlc_skiplist_node **node);
wlc_skiplist_node* wlcsl_find(wlc_skiplist *wlcsl,int wlc,\
		struct datasevr_block_st *dblk);
#if 0
wlc_skiplist_node* wlcsl_find_by_id(wlc_skiplist *wlcsl,uint32_t id);
#endif
int cluster_wlc_block_add(wlc_skiplist *wlcsl,wlc_ctx *wctx);
struct datasevr_block_st* cluster_wlc_master_block_get(wlc_skiplist *wlcsl);

#ifdef __cplusplus
}
#endif
#endif
