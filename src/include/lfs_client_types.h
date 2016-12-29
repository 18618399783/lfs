/**
*
*
*
*
*
*
**/
#ifndef _LFS_CLIENT_TYPES_H_
#define _LFS_CLIENT_TYPES_H_

#include "common_define.h"
#include "lfs_define.h"


#ifdef __cplusplus
extern "C"{
#endif

struct lfs_tracker_resp_st{
	char block_ipaddr[IP_ADDRESS_SIZE];
	char block_port[LFS_STRUCT_PROP_LEN_SIZE8];
};
typedef struct lfs_tracker_resp_st lfs_tracker_resp;

struct lfs_read_tracker_req_st{
	char map_info[LFS_DATASERVER_MAP_INFO_SIZE];
	char file_timestamp[LFS_STRUCT_PROP_LEN_SIZE8];
};
typedef struct lfs_read_tracker_req_st lfs_read_tracker_req;

struct lfs_fileupload_req_st{
	char file_name[LFS_FILE_NAME_SIZE];
	char file_size[LFS_STRUCT_PROP_LEN_SIZE8];
	char file_total_size[LFS_STRUCT_PROP_LEN_SIZE8];
};
typedef struct lfs_fileupload_req_st lfs_fileupload_req;

struct lfs_fileupload_resp_st{
	char file_b64_id[LFS_B64_FILE_ID_SIZE];
};
typedef struct lfs_fileupload_resp_st lfs_fileupload_resp;

struct lfs_filedownload_req_st{
	char file_offset[LFS_STRUCT_PROP_LEN_SIZE8];
	char download_size[LFS_STRUCT_PROP_LEN_SIZE8];
	char mnt_block_index[LFS_STRUCT_PROP_LEN_SIZE8];
	char file_map_name[LFS_FILE_ID_SIZE];
};
typedef struct lfs_filedownload_req_st lfs_filedownload_req;

struct lfs_filedownload_resp_st{
	char download_size[LFS_STRUCT_PROP_LEN_SIZE8];
};
typedef struct lfs_filedownload_resp_st lfs_filedownload_resp;


#ifdef __cplusplus
}
#endif
#endif
