/**
*
*
*
*
*
*
**/
#ifndef _DS_BLOCK_H_
#define _DS_BLOCK_H_

#include "dataserverd.h"
#include "ds_types.h"


#ifdef __cplusplus
extern "C"{
#endif

struct block_st{
	char mount_path[MAX_PATH_SIZE];
	int pre_path_map;
	int suf_path_map;
	int curr_path_map_file_count;
	int total_size_mb;
	int free_size_mb;
};
typedef struct block_st block;

struct block_mount_st{
	struct block_st **blocks;
	int block_total_size_mb;
	int block_free_size_mb;
	int mount_count;
};
typedef struct block_mount_st block_mount;

void MOUNT_BLOCK_LOCK();
void MOUNT_BLOCK_UNLOCK();
int block_stat_flush(void);
int block_vfs_statistics(void);
int mount_block_map_init(void);
void mount_block_map_destroy(void);
int block_dir_map(const char *mount_path);
int mount_block_map_polling(conn *c);

extern block_mount mounts;
#ifdef __cplusplus
}
#endif
#endif
