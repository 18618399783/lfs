/**
*
*
*
*
*
**/
#ifndef _TS_CLUSTER_DATASEVR_H_
#define _TS_CLUSTER_DATASEVR_H_


#include "trackerd.h"
#include "ts_types.h"

#ifdef __cplusplus
extern "C"{
#endif

int cluster_datasevrblock_insert(datasevr_block *dblk);
datasevr_volume* cluster_datasevrvolume_find(const char *vn,size_t nvn);
datasevr_block* cluster_datasevrmasterblock_get(const char *vn,size_t nvn);
datasevr_block* cluster_wlc_writedatasevrblock_get();
datasevr_block* cluster_readdatasevrblock_get(const char *vn,size_t nvn,int64_t timestamp);
datasevr_block* cluster_datasevrblock_find(const char *gn,const size_t ngn,const char *sip,const size_t nsi);

#ifdef __cplusplus
}
#endif
#endif
