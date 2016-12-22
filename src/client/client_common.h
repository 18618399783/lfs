/**
*
*
*
*
*
**/
#ifndef _CLIENT_COMMON_H_
#define _CLIENT_COMMON_H_

#include "lfs_types.h"


#ifdef __cplusplus
extern "C"{
#endif

int file_metedata_unpack(const char *file_b64name,file_metedata *fmete);

#ifdef __cplusplus
}
#endif
#endif
