/**
*
*
*
*
*
*
**/
#ifndef _DS_BINLOG_H_
#define _DS_BINLOG_H_

#include "dataserverd.h"
#include "ds_types.h"


#ifdef __cplusplus
extern "C"{
#endif

#define BINLOG_FILE_NAME "binlog.bat"
#define BINLOG_CACHE_BUFFER_SIZE (16 * 1024)
#define BINLOG_BUFFER_SIZE 64 * 1024

#define BINLOG_FILENAME(pre_binlog_file_name,binlog_index,buff) \
	snprintf(buff,MAX_PATH_SIZE,"%s.%03d",\
			pre_binlog_file_name,binlog_index);

int binlog_init(void);
void binlog_destroy(void);
int local_binlog_init(binlog_ctx *bctx);
int remote_binlog_init(binlog_ctx *bctx);
void binlog_ctx_destroy(binlog_ctx *bctx);
int binlog_ctx_lock(void); 
void binlog_ctx_unlock(void);
int binlog_write(binlog_ctx *bctx,const char *line);
int binlog_flush(binlog_ctx *bctx);
enum binlog_file_state binlog_read(sync_ctx *sctx,binlog_ctx *bctx,binlog_record *brecord,int *brecord_size);
int asyncctx_init(block_brief *bbrief,sync_ctx *sctx,binlog_ctx *bctx);
int fullsyncctx_init(sync_ctx *sctx,binlog_ctx *bctx,connect_info *cinfo);
int sync_mark_file_batdata_write(sync_ctx *sctx);
int binlogsyncctx_destroy(sync_ctx *sctx);


#ifdef __cplusplus
}
#endif
#endif
