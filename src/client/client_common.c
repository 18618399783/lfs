/**
*
*
*
*
*
**/

#include <stdlib.h>
#include <assert.h>
#include "client_common.h"



int file_metedata_unpack(const char *file_b64name,file_metedata *fmete)
{
	assert(fmete != NULL);
	char file_name_b64buff[LFS_FILE_NAME_SIZE] = {0};
	char file_binname[256] = {0};
	//int file_binname_len;
	char *p;

	snprintf(file_name_b64buff,sizeof(file_name_b64buff),"%s",\
			file_b64name);
	//file_binname_len = Base64decode_len((const char*)file_name_b64buff);
	Base64decode(file_binname,file_b64name);
	p = file_binname;
	fmete->f_timestamp = (time_t)buff2long((const char*)p);
	p = p + LFS_FILE_METEDATA_TIME_BUFF_SIZE;
	fmete->f_offset = buff2long((const char*)p);
	p = p + LFS_FILE_METEDATA_OFFSET_BUFF_SIZE;
	fmete->f_size = buff2long((const char*)p);
	p = p + LFS_FILE_METEDATA_SIZE_BUFF_SIZE;
	fmete->f_crc32 = buff2int((const char*)p);
	return LFS_OK;
}
