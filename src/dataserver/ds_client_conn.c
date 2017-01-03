/**
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
#include <assert.h>
#include <stdint.h>
#include <time.h>
#include <sys/types.h>
#include <event.h>



#include "common_define.h"
#include "logger.h"
#include "shared_func.h"
#include "ds_func.h"
#include "ds_client_conn.h"




trackerclient_conn* trackerclientconn_new()
{
	trackerclient_conn *c = NULL;
	c = (trackerclient_conn*)malloc(sizeof(trackerclient_conn));
	if(NULL == c)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"Failed to allocate tracker client connection!", __LINE__);
		return NULL;
	}
	memset(c,0,sizeof(trackerclient_conn));
	c->index = -1;
	c->last_heartbeat_time = 0;
	c->last_reportstate_time = 0;
	return c;
}

void trackerclientconn_close(trackerclient_conn *c)
{
	if(c->sfd >= 0)
		close(c->sfd);
	c->last_heartbeat_time = 0;
	c->last_reportstate_time = 0;
	CTXS_LOCK();
	ctxs.curr_conns--;
	CTXS_UNLOCK();
}

void set_trackerclientconn_state(trackerclient_conn *c,enum trackerclient_state new_state)
{
	assert(c != NULL);
	if(new_state != c->state)
		c->state = new_state;
	return;
}

void update_trackerclientconn_state(trackerclient_conn *c,enum trackerclient_state new_state)
{
	assert(c != NULL);
	c->state = new_state;
	return;
}

