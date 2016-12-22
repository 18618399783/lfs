/**
*
*
*
*
*
**/
#ifndef _DS_QUEUE_H_
#define _DS_QUEUE_H_
#include "list.h"

#ifdef __cplusplus
extern "C"{
#endif

struct queue_head_st{
	pthread_mutex_t lock;	
	struct list_head head;
};
typedef struct queue_head_st queue_head; 

void init_queue(queue_head *cq_head);
int queue_active(queue_head *cq_head);

#ifdef __cplusplus
}
#endif
#endif
