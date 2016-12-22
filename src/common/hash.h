/**
*
*
*
*
*
*
**/

#ifndef _HASH_H_
#define _HASH_H_

#include <stdint.h>
#include "common_define.h"

#ifdef __cplusplus
extern "C"{
#endif

typedef uint32_t (*hash_func_f)(const void *key,size_t length);

extern hash_func_f hash_func;

enum hashfunc_type{
	JENKINS_HASH = 0,MURMUR3_HASH
};

int hash_init(enum hashfunc_type type);

hash_func_f get_hash_func(enum hashfunc_type type);

#ifdef __cplusplus
}
#endif

#endif
