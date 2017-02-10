/**
*
*
*
*
*
*
**/
#ifndef _TRACKER_CLIENT_H_
#define _TRACKER_CLIENT_H_

#include "common_define.h"
#include "lfs_protocol.h"

#ifdef __cplusplus
extern "C"{
#endif

struct tracker_info_st{
	int index;
	int port;
	char ip_addr[IP_ADDRESS_SIZE];
};
typedef struct tracker_info_st tracker_info;

struct tracker_group_st{
	int tracker_count;
	tracker_info *trackers;
};
typedef struct tracker_group_st tracker_group;

int trackerclient_info_init();
int tracker_report_thread_start(void);

extern struct tracker_group_st tracker_groups;
extern block_brief volume_blocks[LFS_MAX_BLOCKS_EACH_VOLUME];
extern int blocks_count;
#ifdef __cplusplus
}
#endif
#endif
