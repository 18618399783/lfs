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

#define BINLOG_FILENAME(binlog_index,fn_buff) \
	snprintf(fn_buff,MAX_PATH_SIZE,"%s/bin/%s.%03d",\
			base_path,BINLOG_FILE_NAME,binlog_index);

int binlog_init(void);

int binlog_destroy(void);

int binlog_write(const char *line);

int curr_binlog_file_index_set(int bindex);

int curr_binlog_file_mete_get(binlog_file_mete *bfmete);

enum binlog_file_state binlog_read(sync_ctx *sctx,binlog_record *brecord,int *brecord_size);

int asyncctx_init(block_brief *bbrief,sync_ctx *sctx);

int fullsyncctx_init(sync_ctx *sctx,connect_info *cinfo);

int sync_mark_file_batdata_write(sync_ctx *sctx);

int binlogsyncctx_destroy(sync_ctx *sctx);


#ifdef __cplusplus
}
#endif
#endif
