/**
*
*
*
*
**/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "common_define.h"
#include "hash.h"
#include "hash_table.h"

int test_for_put(hash_table *pht)
{
	char *key = NULL;
	char *value = NULL;
	char sptf[32] = {0};
	int size = 0;
	int i = 0;
	int result = 0;
	int rnd = 0;

	size = sizeof(char) * (4 + sizeof(int) + 1);
	key = (char*)malloc(size);
	if(key == NULL) goto err;
	memset(key,0,size);
	value = (char*)malloc(size);
	if(value == NULL) goto err;
	memset(value,0,size);

	for(;i < 1;i++)
	{
		//printf("index:%d\n",i);
		memset(sptf,0,32);
		memset(key,0,size);
		memset(value,0,size);
		memcpy(key,"test",4);
		memcpy(value,"test",4);
		srand((unsigned)time(NULL));
		rnd = rand() + i;
		sprintf(sptf,"%d",rnd);
		memcpy(key + 4,sptf,strlen(sptf));
		memcpy(value + 4,sptf,strlen(sptf));
		//printf("rand buff:%s\n",sptf);
		if((result = hash_table_put(pht,(const void*)key,strlen(key),(const void*)value,strlen(value))) != 0)
		{
			printf("hash table put failed.\n");
			goto err;
		}
	}

err:
	if(key != NULL)
	{
		free(key);
		key = NULL;
	}
	if(value != NULL)
	{
		free(value);
		value = NULL;
	}
	return 0;
}

int main(int argc,char **argv)
{
	int result = 0;
	char *key = "base.path";
	char *value = "test/key/value";
	hash_table ht;	
	hash_data *phd_get_value = NULL;

	printf("hash table test\n");
	hash_init(JENKINS_HASH);
	if((result = hash_table_init(&ht,hash_func,0,0)) != 0)
	{
		printf("hash table init failed.\n");
		goto err;
	}

#if 0
	test_for_put(&ht);
#endif

#if 1
	result = 0;
	if((result = hash_table_put(&ht,(const void*)key,strlen(key),(const void*)value,strlen(value))) != 0)
	{
		printf("hash table put failed.\n");
		goto err;
	}
	if((result = hash_table_put(&ht,(const void*)"logger.level",strlen("logger.level"),(const void*)"debug",strlen("debug"))) != 0)
	{
		printf("hash table put failed.\n");
		goto err;
	}
	if((result = hash_table_put(&ht,(const void*)"logger.file.name",strlen("logger.file.name"),(const void*)"trackerd",strlen("trackerd"))) != 0)
	{
		printf("hash table put failed.\n");
		goto err;
	}
	if((result = hash_table_put(&ht,(const void*)"logger.file.test",strlen("logger.file.test"),(const void*)"test",strlen("test"))) != 0)
	{
		printf("hash table put failed.\n");
		goto err;
	}
	if((result = hash_table_put(&ht,(const void*)"test.bug",strlen("test.bug"),(const void*)"bug",strlen("bug"))) != 0)
	{
		printf("hash table put failed.\n");
		goto err;
	}
	hash_table_walk(&ht);
	printf("hash table current parameter,item count:%d,capacity:%d,factor:%f\n", \
			ht.item_count,ht.capacity,ht.load_factor);
	printf("hash table get value\n");
	phd_get_value = hash_table_get(&ht,(const void*)"base.path",strlen("base.path"));
	if(phd_get_value == NULL)
	{
		printf("hash table get failed.\n");
		goto err;
	}
	printf("value:%s\n",phd_get_value->value);
	phd_get_value = NULL;
	phd_get_value = hash_table_get(&ht,(const void*)"logger.level",strlen("logger.level"));
	if(phd_get_value == NULL)
	{
		printf("hash table get failed.\n");
		goto err;
	}
	printf("value:%s\n",phd_get_value->value);
	phd_get_value = NULL;
	phd_get_value = hash_table_get(&ht,(const void*)"logger.file.name",strlen("logger.file.name"));
	if(phd_get_value == NULL)
	{
		printf("hash table get failed.\n");
		goto err;
	}
	printf("value:%s\n",phd_get_value->value);
	phd_get_value = NULL;
	phd_get_value = hash_table_get(&ht,(const void*)"logger.file.test",strlen("logger.file.test"));
	if(phd_get_value == NULL)
	{
		printf("hash table get failed.\n");
		goto err;
	}
	printf("value:%s\n",phd_get_value->value);
	phd_get_value = NULL;
	phd_get_value = hash_table_get(&ht,(const void*)"test.bug",strlen("test.bug"));
	if(phd_get_value == NULL)
	{
		printf("hash table get failed.\n");
		goto err;
	}
	printf("value:%s\n",phd_get_value->value);
/*
	if((result = hash_table_delete(&ht,(const void*)key,strlen(key))) != 0)
	{
		printf("hash table delete failed.\n");
		goto err;
	}
	phd_get_value = NULL;
	phd_get_value = hash_table_get(&ht,(const void*)key,strlen(key));
	if(phd_get_value != NULL)
	{
		printf("hash table get failed.\n");
		goto err;
	}
*/
#endif
err:
	hash_table_free(&ht);
	return 0;
}
