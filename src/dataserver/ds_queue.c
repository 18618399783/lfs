/**
*
*
*
*
*
**/

#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "logger.h"
#include "list.h"
#include "dataserverd.h"
#include "ds_queue.h"


static inline void __init_queue(queue_head *cq_head);
static inline int __queue_active(queue_head *cq_head);

void init_queue(queue_head *cq_head)
{
	__init_queue(cq_head);
}

int queue_active(queue_head *cq_head)
{
	return __queue_active(cq_head);
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

