/**
*
*
*
*
*
*
**/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "common_define.h"
#include "shared_func.h"
#include "hash.h"
#include "cfg_file_opt.h"


static int _read_cfg_file(hash_table *pht,const char *fpath)
{
	int ret = 0;
	char *pcont = NULL;
	char *pline = NULL;
	char *ple = NULL;
	char *pec = NULL;
	char *name = NULL;
	int key_len;
	int value_len;
	off_t file_size;	

	if((ret = (getFileContent(fpath,&pcont,&file_size))) != 0)
		return ret;
	ple = pcont -1;
	while(ple != NULL)
	{
		pline = ple + 1;
		ple = strchr(pline,'\n');
		if(ple != NULL)
			*ple = '\0';
		trim(pline);
		if((*pline == '#') || (*pline == '\0'))
			continue;
		pec = strchr(pline,'=');
		if(pec == NULL)
			continue;
		key_len = pec - pline;
		value_len = strlen(pline) - (key_len + 1);
		name = (char*)malloc(sizeof(char) * (key_len + 1));
		if(name == NULL)
			return -1;
		memset(name,0,(sizeof(char) * key_len + 1));
		memcpy(name,pline,key_len);
		hash_table_put(pht,(const void*)name,(const int)key_len,(const void*)(pec + 1),(const int)value_len);
		free(name);
		name = NULL;
	}
	return ret;
}

int cfg_init(hash_table *pht,const char* fpath)
{
	int ret = 0;
	if((ret = hash_table_init(pht,hash_func,0,0)) != 0)
	{
		fprintf(stderr,"Failed to initialize the %s file's hash table!\n",fpath);
		goto err;
	}
	if((ret = _read_cfg_file(pht,fpath)) != 0)
	{
		fprintf(stderr,"Failed to Read conf file!\n");
		goto err;
	}
	return 0;
err:
	hash_table_free(pht);
	return ret;
}

void cfg_destroy(hash_table *pht)
{
	hash_table_free(pht);
}

char* cfg_get_strvalue(hash_table *pht,const char *name)
{
	hash_data *hd_value = NULL;

	hd_value = hash_table_get(pht,(const void*)name,strlen(name));
	if(!hd_value)
	{
		fprintf(stderr,"Failed to get %s hash table value!\n",name);
		return NULL;
	}
	return hd_value->value;
}

int cfg_get_intvalue(hash_table *pht,const char *name,int dvalue)
{
	char *svalue = NULL;
	svalue = cfg_get_strvalue(pht,name);
	if(!svalue)
	{
		fprintf(stderr,"Failed to get %s hash table value!\n",name);
		return dvalue;
	}
	return atoi(svalue);
}

float cfg_get_floatvalue(hash_table *pht,const char *name,float dvalue)
{
	char *svalue = NULL;
	svalue = cfg_get_strvalue(pht,name);
	if(!svalue)
	{
		fprintf(stderr,"Failed to get %s hash table value!\n",name);
		return dvalue;
	}
	return atof(svalue);
}
