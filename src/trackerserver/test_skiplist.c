/**
*
*
*
*
*
*
**/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "common_define.h"
#include "hash.h"
#include "ts_wlc.h"

void iterator_skiplist(wlc_skiplist *wlcsl,int *rwlc,uint32_t *rid);
void skiplist_find_by_wlc(wlc_skiplist *wlcsl,int wlc,uint32_t id);

int main(int argc,char **argv)
{
	int i;
	int count;
	char *ips[] = {"192.168.1.1","192.168.1.2","192.168.1.3",\
	"192.168.1.4","192.168.1.5","192.168.1.6","192.168.1.7",\
	"192.168.1.8","192.168.1.9","192.168.1.10","192.168.1.11",\
	"192.168.1.12","192.168.1.13","192.168.1.14","192.168.1.15"};
	int wlcs[] = {3,67,1,46,97,2,18,7,36,43,21,32,5,5,8};
	wlc_skiplist *ws;
	uint32_t id;
	int rwlc;
	uint32_t rid; 
	wlc_skiplist_node *x;

	hash_init(MURMUR3_HASH);
	ws = wlcsl_create();
	if(ws == NULL)
	{
		printf("create wlc skiplist failed.\n");
		return -1;
	}
	count = sizeof(ips) / sizeof(char*);	
	for(i = 0;i < count; i++)
	{
		id = hash_func((const void*)ips[i],strlen(ips[i]));
		wlcsl_insert(ws,wlcs[i],id);
	}
	iterator_skiplist(ws,&rwlc,&rid);
	printf("will find wlc:%d,id:%u.\n",rwlc,rid);
	skiplist_find_by_wlc(ws,rwlc,rid);
	//wlcsl_delete(ws,rwlc,rid,NULL);
	iterator_skiplist(ws,&rwlc,&rid);
	x = wlcsl_find_by_id(ws,rid);
	if(x)
	{
		printf("==========wlc:%d,id:%u get by id.\n",x->wlc,x->id);
	}
	return 0;
}

void iterator_skiplist(wlc_skiplist *wlcsl,int *rwlc,uint32_t *rid)
{
	assert(wlcsl != NULL);	
	wlc_skiplist_node *x;
	int i;

	x = wlcsl->header;
	printf("sort:\n");
	while(x->level[0].forward)
	{
		x = x->level[0].forward;
		printf("[%d,%u] ",x->wlc,x->id);	
		if(i == 7)
		{
			*rwlc = x->wlc;
			*rid = x->id;
		}
		i++;
	}
	printf("\n");
}

void skiplist_find_by_wlc(wlc_skiplist *wlcsl,int wlc,uint32_t id)
{
	assert(wlcsl != NULL);	
	wlc_skiplist_node *x;
	x = wlcsl_find(wlcsl,wlc,id);
	if(x != NULL)
		printf("find wlc:%d,id:%u.\n",x->wlc,x->id);
	else
		printf("no find.\n");

}
