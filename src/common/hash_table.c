/**
*
*
*
*
*
**/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "hash_table.h"

#define DEFAULT_INITIAL_CAPACITY 1 << 4
#define MAXINUM_CAPACITY 1 << 30
#define DEFAULT_LOAD_FACTOR 0.75

#define DELETE_NODE_FROM_HASHTABLE(pht,bucket,phd_pre,phd_bucket) \
	if(phd_pre == NULL) \
	{\
		(*bucket) = phd_bucket->next;\
	}\
	else \
	{\
		phd_pre = phd_bucket->next;\
	}\
	pht->item_count--;\
	_free_hash_data(phd_bucket);

#define HASH_CODE(pht,phd) ((unsigned int)pht->hash_func(phd->key,phd->key_len))

#define ADD_NODE_TO_HASHTABLE(pht,bucket,phd) \
	phd->next = (*bucket);\
	(*bucket) = phd;\
	pht->item_count++;

#define INDEX_FOR(h,length) (h & (length - 1))


static void _free_hash_data(hash_data *phd)
{
	if(phd == NULL)
		return;
	if(phd->key != NULL)
	{
		free(phd->key);
		phd->key = NULL;
	}
	if(phd->value != NULL)
	{
		free(phd->value);
		phd->value = NULL;
	}
	free(phd);
	phd = NULL;
	return;
}	

static int _alloc_hash_table_buckets(hash_table *pht)
{
	int size = sizeof(hash_data*) * (pht->capacity);
	pht->table = (hash_data**)malloc(size);
	if(NULL == pht->table) return ENOMEM;
	memset(pht->table,0,size);
	return 0;
}

static int _hash_table_resize(hash_table *pht,const int new_capacity)
{
	hash_data **pphd_old_table = NULL;
	hash_data **pphd_temp = NULL;
	hash_data **pphd_old_table_end = NULL;
	hash_data *phd_temp = NULL;
	hash_data *phd_next = NULL;
	hash_data **pphd_new_table = NULL;
	int old_capacity = 0;

	old_capacity = pht->capacity;
	pphd_old_table = pht->table;
	pht->capacity = new_capacity;

	pphd_new_table = (hash_data**)malloc(sizeof(hash_data*) * new_capacity);
	if(pphd_new_table == NULL)
	{
		pht->capacity = old_capacity;
		return ENOMEM;
	}
	memset(pphd_new_table,0,sizeof(hash_data*) * new_capacity);
	pht->item_count = 0;
	pphd_old_table_end = pphd_old_table + old_capacity;
	for(pphd_temp = pphd_old_table;pphd_temp < pphd_old_table_end;pphd_temp++)
	{
		if(*pphd_temp == NULL) continue;
		phd_temp = *pphd_temp;
		while(phd_temp != NULL)
		{
			phd_next = phd_temp->next;
			ADD_NODE_TO_HASHTABLE(pht,(pphd_new_table + INDEX_FOR(HASH_CODE(pht,phd_temp),pht->capacity)),phd_temp)
			phd_temp = phd_next;
		}
	}
	pht->threshold = (int)(pht->capacity * pht->load_factor);
	free(pht->table);
	pht->table = NULL;
	pht->table = pphd_new_table;
	return 0;
}

static hash_data* _bucket_find_entry(hash_data **bucket,const void *key,const int key_len)
{
	hash_data *phd_next = NULL;

	phd_next = *bucket;
	while(phd_next != NULL)
	{
		if(key_len == phd_next->key_len && \
				memcmp(key,phd_next->key,key_len) == 0)
			return phd_next;
		phd_next = phd_next->next;
	}
	return NULL;
}

static int _bucket_add_entry(hash_table *pht,hash_data **bucket,const void *key,const int key_len,const void *value,const int value_len)
{
	hash_data *phd = NULL;
	hash_data *phd_bucket = NULL;
	hash_data *phd_pre = NULL;
	unsigned int hash_code = 0;

	phd_bucket = *bucket;
	while(phd_bucket != NULL)
	{
		if(key_len == phd_bucket->key_len && \
				memcmp(key,phd_bucket->key,key_len) == 0)
			break;
		phd_pre = phd_bucket;
		phd_bucket = phd_bucket->next;
	}
	if(phd_bucket != NULL)
	{
		DELETE_NODE_FROM_HASHTABLE(pht,bucket,phd_pre,phd_bucket)
	}
	phd = (hash_data*)malloc(sizeof(hash_data)); 
	if(phd == NULL)
		return ENOMEM;
	memset(phd,0,sizeof(hash_data)); 
	phd->next = NULL;
	phd->key = (char*)malloc(sizeof(char) * (key_len + 1));
	if(phd->key == NULL)
		return -1;
	memset(phd->key,0,sizeof(char) * (key_len + 1));
	phd->value = (char*)malloc(sizeof(char) * (value_len + 1));
	if(phd->value == NULL)
		return -1;
	memset(phd->value,0,sizeof(char) * (value_len + 1));
	hash_code = pht->hash_func(key,(size_t)key_len);
	phd->hash_code = hash_code;
	memcpy(phd->key,key,key_len);
	phd->key_len = key_len;
	memcpy(phd->value,value,value_len);
	phd->value_len = value_len;
	ADD_NODE_TO_HASHTABLE(pht,bucket,phd)

	return 0;
}

int hash_table_init(hash_table *pht,hash_func_f hash_func,\
		const int init_capacity,const double load_factor)
{
	int result = 0;
	if(pht == NULL) return EINVAL;
	if(init_capacity < 0) return EINVAL;
	if(load_factor < 0) return EINVAL;
	int capacity = 1;
	int target_capacity = DEFAULT_INITIAL_CAPACITY;
	

	memset(pht,0,sizeof(hash_table));
	pht->hash_func = hash_func;
	if(init_capacity == 0)
	{
		target_capacity = DEFAULT_INITIAL_CAPACITY;
	}
	else if(init_capacity > MAXINUM_CAPACITY)
	{
		target_capacity = MAXINUM_CAPACITY; 
	}
	else
	{
		target_capacity = init_capacity;
	}
	if(load_factor >= 0.10 && load_factor <= 1.00)
	{
		pht->load_factor = load_factor;
	}
	else
	{
		pht->load_factor = DEFAULT_LOAD_FACTOR;
	}
	while(capacity < target_capacity)
		capacity <<= 1;
	pht->capacity = capacity;
	pht->threshold = (int)(pht->capacity * pht->load_factor);
	pht->item_count = 0;
	if((result = _alloc_hash_table_buckets(pht)) != 0) return result;
	return LFS_OK;
}

void hash_table_walk(hash_table *pht)
{
	if(pht == NULL || pht->table == NULL) return;
	hash_data **pphd_table_end = NULL;
	hash_data **pphd_table = NULL;
	hash_data *phd_next = NULL;
	hash_data *phd_temp = NULL;

	pphd_table = pht->table;
	pphd_table_end = pht->table + pht->capacity;
	for(;pphd_table < pphd_table_end;pphd_table++)
	{
		phd_temp = *pphd_table;
		if(phd_temp == NULL) continue;
		while(phd_temp != NULL)
		{
			phd_next = phd_temp->next;
			phd_temp = phd_next;
		}
	}
	return;
}

void hash_table_free(hash_table *pht)
{
	if(pht == NULL || pht->table == NULL) return;
	hash_data **pphd_table_end = NULL;
	hash_data **pphd_table = NULL;
	hash_data *phd_next = NULL;
	hash_data *phd_temp = NULL;

	pphd_table = pht->table;
	pphd_table_end = pht->table + pht->capacity;
	for(;pphd_table < pphd_table_end;pphd_table++)
	{
		phd_temp = *pphd_table;
		if(phd_temp == NULL) continue;
		while(phd_temp != NULL)
		{
			phd_next = phd_temp->next;
			_free_hash_data(phd_temp);
			phd_temp = phd_next;
		}
	}
	free(pht->table);
	pht->table = NULL;
	return;
}

int hash_table_put(hash_table *pht,const void *key,const int key_len,const void *value,const int value_len)
{
	unsigned int hash_code;
	hash_data **pphd_bucket;
	int result = 0;

	if(pht->item_count >= pht->threshold)
	{
		_hash_table_resize(pht,2 * pht->capacity);
	}
	hash_code = pht->hash_func(key,(size_t)key_len);
	pphd_bucket = pht->table + INDEX_FOR(hash_code,pht->capacity);

	if((result = _bucket_add_entry(pht,pphd_bucket,key,key_len,value,value_len) != 0))
		goto err;
	return LFS_OK;
err:
	return LFS_ERROR;
}

void* hash_table_get(hash_table *pht,const void *key,const int key_len)
{
	unsigned int hash_code = 0;
	hash_data **pphd_bucket = NULL;
	hash_data *phd_temp = NULL;

	hash_code = pht->hash_func(key,(size_t)key_len);
	pphd_bucket = pht->table + INDEX_FOR(hash_code,pht->capacity);
	phd_temp = _bucket_find_entry(pphd_bucket,key,key_len);
	if(phd_temp != NULL)
	{
		return phd_temp;
	}
	return NULL;
}

int hash_table_delete(hash_table *pht,const void *key,const int key_len)
{
	hash_data *phd_bucket = NULL;
	hash_data *phd_pre = NULL;
	unsigned int hash_code;
	hash_data **pphd_bucket;
	int result = LFS_OK;

	hash_code = pht->hash_func(key,(size_t)key_len);
	pphd_bucket = pht->table + INDEX_FOR(hash_code,pht->capacity);

	phd_bucket = *pphd_bucket;
	while(phd_bucket != NULL)
	{
		if(key_len == phd_bucket->key_len && \
				memcmp(key,phd_bucket->key,key_len) == 0)
			break;
		phd_pre = phd_bucket;
		phd_bucket = phd_bucket->next;
	}
	if(phd_bucket != NULL)
	{
		DELETE_NODE_FROM_HASHTABLE(pht,pphd_bucket,phd_pre,phd_bucket)
	}
	else
	{
		result = LFS_ERROR;	
	}
	return result;
}

int hash_table_count(hash_table *pht)
{
	if(pht == NULL) return 0;
	return pht->item_count;
}
