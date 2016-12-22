
/**
*
*
*
*
*
**/

#ifndef _TS_SERVICE_H_
#define _TS_SERVICE_H_

#include "trackerd.h"
#include "lfs_protocol.h"

#ifdef __cplusplus
extern "C"{
#endif

protocol_resp_status handle_cmd_register(conn *c);
protocol_resp_status handle_cmd_fullsyncreq(conn *c);
protocol_resp_status handle_cmd_heartbeat(conn *c);
protocol_resp_status handle_cmd_syncreport(conn *c);
protocol_resp_status handle_cmd_statreport(conn *c);
protocol_resp_status handle_cmd_quited(conn *c);

protocol_resp_status handle_cmd_writetracker(conn *c);
protocol_resp_status handle_cmd_readtracker(conn *c);

void handle_protocol_error(conn *c,protocol_resp_status resp_status);

protocol_resp_status handle_cmd_test(conn *c);
#ifdef __cplusplus
}
#endif
#endif
