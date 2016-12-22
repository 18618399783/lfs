
/**
*
*
*
*
*
**/

#ifndef _DS_SERVICE_H_
#define _DS_SERVICE_H_

#include "dataserverd.h"
#include "lfs_protocol.h"

#ifdef __cplusplus
extern "C"{
#endif

void handle_protocol_error(conn *c,protocol_resp_status resp_status);
void handle_protocol_ack(conn *c,enum conn_states cstates,enum conn_states nextto);
protocol_resp_status handle_cmd_fileupload(conn *c);
protocol_resp_status handle_cmd_filedownload(conn *c);
protocol_resp_status handle_cmd_asynccopyfile(conn *c);
protocol_resp_status handle_cmd_getmasterbinlogmete(conn *c);
protocol_resp_status handle_cmd_copymasterbinlog(conn *c);
protocol_resp_status handle_cmd_copymasterdata(conn *c);
void file_upload_done_callback(conn *c,const int err_no);
void asyncfile_done_callback(conn *c,const int err_no);
void file_download_done_callback(conn *c,const int err_no);

#ifdef __cplusplus
}
#endif
#endif
