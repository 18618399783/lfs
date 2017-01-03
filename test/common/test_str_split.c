/**
*
*
*
*
*
**/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common_define.h"
#include "hash.h"
#include "hash_table.h"

struct ip_info_st{
	int index;
	int port;
	char ipaddr[16];
};

struct ips_st{
	int count;
   struct ip_info_st *ips;	
};

struct ips_st g_ip_info;

int split(char *str)
{
	int in = 0,index = 0;
	const char delim_outer[2] = "|";
	const char delim_inner[2] = ":";
	char *buff = NULL;
	char *outer_ptr = NULL;
	char *inner_ptr = NULL;
	char *p[32];

	buff = str;
	while((p[in] = strtok_r(buff,delim_outer,&outer_ptr)) != NULL)
	{
		buff = p[in];
	   while((p[in] = strtok_r(buff,delim_inner,&inner_ptr)) != NULL)	
	   {
		   if((in % 2) == 0)
		   {
			   memcpy(g_ip_info.ips[index].ipaddr,p[in],strlen(p[in]));
		   }
		   else if((in % 2) == 1)
		   {
			   g_ip_info.ips[index].port = atoi(p[in]);
		   }
		   in++;
		   buff = NULL;
	   }
	   g_ip_info.ips[index].index = index;
	   index++;
	   buff = NULL;
	}
	return 0;
}

int main(int argc,char **argv)
{
	int result;
	int i;
	char *key = "trackers";
	char *value = "192.168.2.136:2936|192.168.2.133:2983|192.168.2.134:2984";
	hash_table ht;	
	hash_data *hash_value = NULL;
	char *str;

	hash_init(JENKINS_HASH);
	if((result = hash_table_init(&ht,hash_func,0,0)) != 0)
	{
		printf("hash table init failed.\n");
		goto err;
	}
	if((result = hash_table_put(&ht,(const void*)key,strlen(key),(const void*)value,strlen(value))) != 0)
	{
		printf("hash table put failed.\n");
		goto err;
	}
	g_ip_info.ips = (struct ip_info_st*)malloc(sizeof(struct ip_info_st) * 3);
	if(g_ip_info.ips == NULL)
	{
		printf("allocate memory faile.\n");
		return -1;
	}
	hash_value = hash_table_get(&ht,(const void*)key,strlen(key)); 
	if(hash_value == NULL)
	{
		printf("hash value is null.\n");
		goto err;
	}
	str = hash_value->value;
	printf("str:%s\n",str);
	split(str);
	for(i = 0;i < 3;i++)
	{
		printf("index:%d,ip:%s:%d\n",g_ip_info.ips[i].index,g_ip_info.ips[i].ipaddr,g_ip_info.ips[i].port);
	}

err:
	if(g_ip_info.ips != NULL)
		free(g_ip_info.ips);
	hash_table_free(&ht);
	return 0;
}
