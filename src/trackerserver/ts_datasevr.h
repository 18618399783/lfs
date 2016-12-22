/**
*
*
*
*
*
**/
#ifndef _TS_DATASEVR_H_
#define _TS_DATASEVR_H_


#include "trackerd.h"
#include "ts_types.h"
#include "lfs_protocol.h"

#ifdef __cplusplus
extern "C"{
#endif

datasevr_block* block_new();
void block_free(datasevr_block *dblk);
datasevr_volume* volume_new();
void volume_free(datasevr_volume *dv);
int do_block_insert(datasevr_volume *dv,datasevr_block *dblk);
datasevr_block* do_block_find(datasevr_volume *dv,uint32_t bid);

#ifdef __cplusplus
}
#endif
#endif
