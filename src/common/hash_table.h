/**
*
*
*
**/

#ifndef _HASH_TABLE_H_
#define _HASH_TABLE_H_

#include "common_define.h"
#include "hash.h"

#ifdef __cplusplus
extern "C"{
#endif

typedef struct hash_data_st {
	int key_len;
	int value_len;
	unsigned int hash_code;
	char *key;
	char *value;
	struct hash_data_st *next;
}hash_data;

typedef struct hash_table_st {
	hash_data **table;
	hash_func_f hash_func;
	int item_count;
	unsigned int capacity;
	double load_factor;
	int threshold;
}hash_table;


int hash_table_init(hash_table *pht,hash_func_f hash_func,\
		const int init_capacity,const double load_factor);

void hash_table_free(hash_table *pht);

int hash_table_put(hash_table *pht,const void *key,const int key_len,const void *value,const int value_len);

void* hash_table_get(hash_table *pht,const void *key,const int key_len);

int hash_table_delete(hash_table *pht,const void *key,const int key_len);

int hash_table_count(hash_table *pht);

void hash_table_walk(hash_table *pht);

#ifdef __cplusplus
}
#endif

#endif

