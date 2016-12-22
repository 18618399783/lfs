/**
*
*
*
*
*
**/
#ifndef _CFG_FILE_OPT_H_
#define _CFG_FILE_OPT_H_
#include "hash_table.h"


#ifdef __cplusplus
extern "C"{
#endif

int cfg_init(hash_table *pht,const char *fpath);

void cfg_destroy(hash_table *pht);

char* cfg_get_strvalue(hash_table *pht,const char *name);

int cfg_get_intvalue(hash_table *pht,const char *name,int dvalue);

float cfg_get_floatvalue(hash_table *pht,const char *name,float dvalue);

#ifdef __cplusplus
}
#endif

#endif
