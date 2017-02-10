/**
*
*
*
*
*
**/

#ifndef _LFS_DEFINE_H_
#define _LFS_DEFINE_H_

#include "common_define.h"
#ifdef __cplusplus
extern "C"{
#endif 
#define LFS_FILE_METEDATA_TIME_BUFF_SIZE 8
#define LFS_FILE_METEDATA_SIZE_BUFF_SIZE 8
#define LFS_FILE_METEDATA_OFFSET_BUFF_SIZE 8
#define LFS_FILE_METEDATA_CRC32_BUFF_SIZE 4
#define LFS_FILE_METEDATA_NAME_BUFF_SIZE 32

#define LFS_GROUP_NAME_LEN_SIZE 16
#define LFS_VOLUME_NAME_LEN_SIZE 16
#define LFS_DATASERVER_MAP_INFO_SIZE 33
#define LFS_STRUCT_PROP_LEN_SIZE8 8
#define LFS_STRUCT_PROP_LEN_SIZE4 4
#define LFS_FILE_ID_SIZE 256
#define LFS_B64_FILE_ID_SIZE 256
#define LFS_FILE_NAME_SIZE 128
#define LFS_FILE_MAP_NAME_SIZE 256
#define LFS_FILE_BLOCK_MAP_NAME_SIZE 256
#define LFS_FILE_ID_SEPERATOR '/'
#define LFS_WRITE_BUFF_SIZE 256 * 1024
#define LFS_MAX_BLOCKS_EACH_VOLUME 3

#define LFS_SPLIT_FILE_MAP_NAME(file_map_name)\
	char f_map_name_buff[LFS_FILE_MAP_NAME_SIZE] = {0};\
	char fsuf_map_name_buff[LFS_FILE_MAP_NAME_SIZE] = {0};\
	char f_name_buff[LFS_FILE_NAME_SIZE] = {0};\
	char *p;\
	\
	snprintf(f_map_name_buff,sizeof(f_map_name_buff),"%s",file_map_name);\
	p = strchr(f_map_name_buff,LFS_FILE_ID_SEPERATOR);\
	if(p == NULL)\
	{\
		return LFS_ERROR;\
	}\
	*p = '\0';\
	snprintf(fsuf_map_name_buff,sizeof(fsuf_map_name_buff),"%s",p + 1);\
	p = strchr(fsuf_map_name_buff,LFS_FILE_ID_SEPERATOR);\
	if(p == NULL)\
	{\
		return LFS_ERROR;\
	}\
	*p = '\0';\
	sprintf(f_name_buff,"%s",p + 1);\

	


#define LFS_SPLIT_BLOCK_INDEX_BY_FILE_ID(file_id)\
	char fid_buff[LFS_FILE_ID_SIZE] = {0};\
	char fid_vmn_buff[LFS_FILE_NAME_SIZE] = {0};\
	char fid_mn_buff[LFS_FILE_NAME_SIZE] = {0};\
	char *pblock_index;\
	char fid_map_name_buff[LFS_FILE_MAP_NAME_SIZE] = {0};\
	char *p;\
	\
	snprintf(fid_buff,sizeof(fid_buff),"%s",file_id);\
	\
	p = strchr(fid_buff,LFS_FILE_ID_SEPERATOR);\
	if(p == NULL)\
	{\
		return LFS_ERROR;\
	}\
	*p = '\0';\
	snprintf(fid_vmn_buff,sizeof(fid_vmn_buff),"%s",p + 1);\
	\
	p = strchr(fid_vmn_buff,LFS_FILE_ID_SEPERATOR);\
	if(p == NULL)\
	{\
		return LFS_ERROR;\
	}\
	*p = '\0';\
	snprintf(fid_mn_buff,sizeof(fid_mn_buff),"%s",p + 1);\
	p = strchr(fid_mn_buff,LFS_FILE_ID_SEPERATOR);\
	if(p == NULL)\
	{\
		return LFS_ERROR;\
	}\
	*p = '\0';\
	pblock_index = fid_mn_buff + 1;\
	sprintf(fid_map_name_buff,"%s",p + 1);\

#define LFS_SPLIT_VOLUME_NAME_AND_BLOCK_INDEX_BY_FILE_ID(file_id)\
	char fid_buff[LFS_FILE_ID_SIZE] = {0};\
	char fid_vmn_buff[LFS_FILE_NAME_SIZE] = {0};\
	char fid_mn_buff[LFS_FILE_NAME_SIZE] = {0};\
	char *volume_name;\
	char *pblock_index;\
	char fid_map_name_buff[LFS_FILE_MAP_NAME_SIZE] = {0};\
	char *p;\
	\
	snprintf(fid_buff,sizeof(fid_buff),"%s",file_id);\
	\
	p = strchr(fid_buff,LFS_FILE_ID_SEPERATOR);\
	if(p == NULL)\
	{\
		return LFS_ERROR;\
	}\
	*p = '\0';\
	snprintf(fid_vmn_buff,sizeof(fid_vmn_buff),"%s",p + 1);\
	\
	p = strchr(fid_vmn_buff,LFS_FILE_ID_SEPERATOR);\
	if(p == NULL)\
	{\
		return LFS_ERROR;\
	}\
	*p = '\0';\
	volume_name = fid_vmn_buff;\
	snprintf(fid_mn_buff,sizeof(fid_mn_buff),"%s",p + 1);\
	\
	p = strchr(fid_mn_buff,LFS_FILE_ID_SEPERATOR);\
	if(p == NULL)\
	{\
		return LFS_ERROR;\
	}\
	*p = '\0';\
	pblock_index = fid_mn_buff + 1;\
	sprintf(fid_map_name_buff,"%s",p + 1);\

#ifdef __cplusplus
}
#endif
#endif
