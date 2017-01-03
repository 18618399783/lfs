/**
*
*
*
*
*
*
**/

#ifndef _DS_CLIENT_CONN_H_
#define _DS_CLIENT_CONN_H_

#include <time.h>

#include "list.h"
#include "lfs_protocol.h"

#ifdef __cplusplus
extern "C"{
#endif

enum trackerclient_state{
	connectd,
	registerd,
	fullsync,
	reporting,
	quited,
	closed
};


struct trackerclient_conn_st{
	int sfd;
	int index;
	time_t last_heartbeat_time;
	time_t last_syncreport_time;
	time_t last_reportstate_time;
	enum trackerclient_state state; 
	enum trackerclient_state nextto;
};
typedef struct trackerclient_conn_st trackerclient_conn;


trackerclient_conn* trackerclientconn_new();
void trackerclientconn_close(trackerclient_conn *c);
void set_trackerclientconn_state(trackerclient_conn *c,enum trackerclient_state new_state);
void update_trackerclientconn_state(trackerclient_conn *c,enum trackerclient_state new_state);

#ifdef __cplusplus
}
#endif
#endif
