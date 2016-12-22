/**
*
*
*
*
*
**/

#include "jenkins_hash.h"
#include "murmur3_hash.h"
#include "hash.h"

hash_func_f hash_func;

int hash_init(enum hashfunc_type type)
{
	switch(type){
		case JENKINS_HASH:
			hash_func = jenkins_hash;
			break;
		case MURMUR3_HASH:
			hash_func = MurmurHash3_x86_32;
			break;
		default:
			return -1;
	}
	return LFS_OK;
}

hash_func_f get_hash_func(enum hashfunc_type type)
{
	switch(type){
		case JENKINS_HASH:
			return jenkins_hash;
			break;
		case MURMUR3_HASH:
			return MurmurHash3_x86_32;
			break;
		default:
			return NULL;
	}
	return NULL;
}


