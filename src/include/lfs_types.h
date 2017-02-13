/**
*
*
*
*
*
**/

#ifndef _LFS_TYPES_H_
#define _LFS_TYPES_H_

#include "common_define.h"
#include "lfs_define.h"

enum datasevr_type{
	master_server = 0,
	slave_server
};

enum datasevr_state{
	init = 0,
	wait_sync,
	syncing,
	on_line,
	active,
	off_line,
	deleted
};

typedef struct datasevr_reg_body_req_st{
	char map_info[LFS_DATASERVER_MAP_INFO_SIZE + 1];
	char ds_ipaddr[IP_ADDRESS_SIZE];
	char ds_port[LFS_STRUCT_PROP_LEN_SIZE8];
	char weight[LFS_STRUCT_PROP_LEN_SIZE8];
	char heart_beat_interval[LFS_STRUCT_PROP_LEN_SIZE8];
	char reg_time[LFS_STRUCT_PROP_LEN_SIZE8];
	char started_time[LFS_STRUCT_PROP_LEN_SIZE8];
}datasevr_reg_body_req;

typedef struct datasevr_reg_body_resp_st{
	char server_type[LFS_STRUCT_PROP_LEN_SIZE4];
}datasevr_reg_body_resp;

typedef struct datasevr_fullsyncreq_body_req_st{
	char map_info[LFS_DATASERVER_MAP_INFO_SIZE + 1];
	char ds_ipaddr[IP_ADDRESS_SIZE];
}datasevr_fullsyncreq_body_req;

typedef struct datasevr_masterblock_body_resp_st{
	char master_ipaddr[IP_ADDRESS_SIZE];
	char master_port[LFS_STRUCT_PROP_LEN_SIZE8];
	char full_sync_opver[LFS_STRUCT_PROP_LEN_SIZE8];
}datasevr_masterblock_body_resp;

typedef struct datasevr_heartbeat_body_req_st{
	char map_info[LFS_DATASERVER_MAP_INFO_SIZE + 1];
	char ds_ipaddr[IP_ADDRESS_SIZE];
}datasevr_heartbeat_body_req;

typedef struct datasevr_syncreport_body_req_st{
	char map_info[LFS_DATASERVER_MAP_INFO_SIZE + 1];
	char ds_ipaddr[IP_ADDRESS_SIZE];
	char last_sync_sequence[LFS_STRUCT_PROP_LEN_SIZE8];
}datasevr_syncreport_body_req;

typedef struct datasevr_statreport_body_req_st{
	char map_info[LFS_DATASERVER_MAP_INFO_SIZE + 1];
	char ds_ipaddr[IP_ADDRESS_SIZE];
	char conns[LFS_STRUCT_PROP_LEN_SIZE8];
	char total_mb[LFS_STRUCT_PROP_LEN_SIZE8];
	char free_mb[LFS_STRUCT_PROP_LEN_SIZE8];
}datasevr_statreport_body_req;

typedef struct datasevr_quited_body_req_st{
	char map_info[LFS_DATASERVER_MAP_INFO_SIZE + 1];
	char ds_ipaddr[IP_ADDRESS_SIZE];
}datasevr_quited_body_req;

typedef struct block_brief_info_resp_st{
	char state[LFS_STRUCT_PROP_LEN_SIZE8];
	char ds_ipaddr[IP_ADDRESS_SIZE];
	char ds_port[LFS_STRUCT_PROP_LEN_SIZE8];
	char last_sync_sequence[LFS_STRUCT_PROP_LEN_SIZE8];
}block_brief_info_resp;

typedef struct file_metedata_st{
	time_t f_timestamp;
	int64_t f_offset;
	int64_t f_size;
	int f_crc32;
}file_metedata;

#endif
