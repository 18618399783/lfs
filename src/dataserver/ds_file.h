/**
*
*
*
*
*
*
**/
#ifndef _DS_FILE_H_
#define _DS_FILE_H_

#include "dataserverd.h"
#include "ds_types.h"


#ifdef __cplusplus
extern "C"{
#endif

int file_ctx_mpools_init(void);
void file_ctx_mpools_destroy(void);
file_ctx* file_ctx_new(const int sfd,const int buffer_size,enum file_op_type opt);
void file_ctx_clean(file_ctx *fctx);
void file_ctx_reset(file_ctx *fctx);
void file_ctx_buff_reset(file_ctx *fctx);
void file_ctx_free(file_ctx *fctx);
int file_unlink(file_ctx *fctx);
int file_metedata_pack(file_ctx *fctx);
int file_metedata_unpack(const char *file_b64name,file_metedata *fmete);
int64_t get_filesize_by_name(const char *file_name);

extern file_ctx **fctxs;

#ifdef __cplusplus
}
#endif
#endif
