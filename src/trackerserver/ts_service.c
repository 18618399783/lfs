/**
*
*
*
*
*
*
**/
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "common_define.h"
#include "hash.h"
#include "logger.h"
#include "shared_func.h"
#include "ts_conn.h"
#include "lfs_protocol.h"
#include "lfs_types.h"
#include "ts_types.h"
#include "ts_wlc.h"
#include "ts_datasevr.h"
#include "ts_cluster.h"
#include "ts_cluster_datasevr.h"
#include "lfs_client_types.h"
#include "ts_service.h"


static void write_error_msg(conn *c,protocol_resp_status resp_status);


void handle_protocol_error(conn *c,protocol_resp_status resp_status)
{
	if(c == NULL) return;
	write_error_msg(c,resp_status);
	set_conn_state(c,conn_write);
	return;
}

protocol_resp_status handle_cmd_register(conn *c)
{
	protocol_resp_status status = PROTOCOL_RESP_STATUS_SUCCESS;
	datasevr_block *dblk = NULL;
	datasevr_reg_body_req *req;
	int ret;
	uint32_t vid,bid;
	
	req = (datasevr_reg_body_req*)c->rcurr;
	vid = hash_func((const void*)req->map_info,strlen(req->map_info));
	bid = hash_func((const void*)req->ds_ipaddr,(size_t)strlen(req->ds_ipaddr));
	dblk = cluster_datasevrblock_byid_find(vid,bid);
	if(dblk == NULL)
	{
		dblk = block_new();
		if(dblk == NULL)
		{
			logger_error("file: "__FILE__", line: %d, " \
					"New datasevr_block struct failed!", __LINE__);
			return status = PROTOCOL_RESP_STATUS_ERROR_MEMORY;
		}
		dblk->state = init;
		memcpy(dblk->ip_addr,req->ds_ipaddr,IP_ADDRESS_SIZE);
		memcpy(dblk->map_info,req->map_info,strlen(req->map_info));
		dblk->port = (int)buff2long(req->ds_port);
		dblk->weight = (int)buff2long(req->weight);
		dblk->heart_beat_interval = (int)buff2long(req->heart_beat_interval);
		dblk->reg_time = (time_t)buff2long(req->reg_time);
		dblk->started_time = (time_t)buff2long(req->started_time);
		dblk->block_id = bid;
		dblk->parent_volume_id = vid;
		if((ret = cluster_datasevrblock_insert(dblk)) != 0)
		{
			block_free(dblk);	
			return status = PROTOCOL_RESP_STATUS_ERROR;
		}
	}
	dblk->state = init;
	dblk->weight = (int)buff2long(req->weight);
	dblk->heart_beat_interval = (int)buff2long(req->heart_beat_interval);

	protocol_header *procl_header;
	procl_header = (protocol_header*)c->wbuff;
	procl_header->header_s.body_len = 0x00;
	procl_header->header_s.cmd = (uint8_t)PROTOCOL_CMD_DATASERVER_REGISTER;
	procl_header->header_s.state = (uint8_t)PROTOCOL_RESP_STATUS_SUCCESS;
	c->wbytes = sizeof(protocol_header);
	set_conn_state(c,conn_write);
	return status;
}

protocol_resp_status handle_cmd_fullsyncreq(conn *c)
{
	protocol_resp_status status = PROTOCOL_RESP_STATUS_SUCCESS;
	datasevr_block *master = NULL;
	datasevr_fullsyncreq_body_req *req;
	datasevr_masterblock_body_resp *resp;
	protocol_header *procl_header;

	req = (datasevr_fullsyncreq_body_req*)c->rcurr;

	procl_header = (protocol_header*)c->wbuff;
	procl_header->header_s.body_len = (uint64_t)sizeof(datasevr_masterblock_body_resp);
	procl_header->header_s.cmd = (uint8_t)PROTOCOL_CMD_DATASERVER_FULLSYNC;
	procl_header->header_s.state = (uint8_t)PROTOCOL_RESP_STATUS_SUCCESS;
	c->wbytes = sizeof(protocol_header);

	datasevr_block *dblk;
	dblk = cluster_datasevrblock_find((const char*)req->map_info,\
			(size_t)strlen(req->map_info),(const void*)req->ds_ipaddr,\
			(size_t)strlen(req->ds_ipaddr));
	if(dblk == NULL)
	{
		status = PROTOCOL_RESP_STATUS_ERROR_UNREGISTER;
		return status;
	}
	
	master = cluster_datasevrmasterblock_get((const char*)req->map_info,(size_t)strlen(req->map_info));
	if(master == NULL)
	{
		dblk->state = wait_sync;
		status = PROTOCOL_RESP_STATUS_SYNCSETTING_WAITING;
		return status;
	}
	dblk->state = syncing;
	resp = (datasevr_masterblock_body_resp*)(c->wbuff + sizeof(protocol_header));
	memcpy(resp->master_ipaddr,master->ip_addr,strlen(master->ip_addr));
	long2buff((int64_t)master->port,resp->master_port);
	long2buff((int64_t)time(NULL),resp->full_sync_opver);
	c->wbytes += sizeof(datasevr_masterblock_body_resp);

	set_conn_state(c,conn_write);
	return status;
}

protocol_resp_status handle_cmd_heartbeat(conn *c)
{
	protocol_resp_status status = PROTOCOL_RESP_STATUS_SUCCESS;
	datasevr_heartbeat_body_req *req_body;
	datasevr_block *dblk = NULL;
	datasevr_volume *dv = NULL;
	time_t current_time = time(NULL);
	
	req_body = (datasevr_heartbeat_body_req*)c->rcurr;
	dblk = cluster_datasevrblock_find((const char*)req_body->map_info,\
			(size_t)strlen(req_body->map_info),(const void*)req_body->ds_ipaddr,\
			(size_t)strlen(req_body->ds_ipaddr));
	if(dblk == NULL)
	{
		status = PROTOCOL_RESP_STATUS_ERROR_UNREGISTER; 
		return status;
	}
	dblk->last_heartbeat_time = current_time;
	dblk->state = active;
	
	protocol_header procl_header = {
		//.header_s.body_len = 0x00,
		.header_s.cmd = (uint8_t)PROTOCOL_CMD_DATASERVER_HEARTBEAT,
		.header_s.state = (uint8_t)PROTOCOL_RESP_STATUS_SUCCESS
	};
	dv = cluster_datasevrvolume_find((const char*)req_body->map_info,(size_t)strlen(req_body->map_info));
	if(dv == NULL)
	{
		status = PROTOCOL_RESP_STATUS_ERROR_NONEDATASEVERVOLUME; 
		return status;
	}
	if(dv->blocks == NULL)
	{
		status = PROTOCOL_RESP_STATUS_ERROR_NONEDATASEVERBLOCKS; 
		return status;
	}
	if(dv->block_count > 1)
	{
		datasevr_block **ites;
		datasevr_block **itee;
		block_brief_info_resp *resp;
		char *p;
		int body_len = 0;

		ites = dv->blocks;
		itee = dv->blocks + dv->block_count;
		p = c->wbuff + sizeof(protocol_header);
		for(; ites < itee; ites++)
		{
			if(strcmp(req_body->ds_ipaddr,(*ites)->ip_addr) == 0)
			{
				continue;
			}
			current_time = time(NULL);
			if((current_time - dblk->last_heartbeat_time) > \
					confitems.heart_beat_interval)
			{
				(*ites)->state = off_line;
			}
			resp = (block_brief_info_resp*)p;
			long2buff((int64_t)((*ites)->state),resp->state);
			memcpy(resp->ds_ipaddr,(*ites)->ip_addr,strlen((*ites)->ip_addr));
			long2buff((int64_t)(*ites)->port,resp->ds_port);
			long2buff((int64_t)(*ites)->last_sync_sequence,resp->last_sync_sequence);
			body_len += sizeof(block_brief_info_resp);
			p = p + body_len;
		}
		procl_header.header_s.body_len = body_len;
		memcpy(c->wbuff,procl_header.header_bytes,sizeof(procl_header.header_bytes));
		c->wbytes = sizeof(protocol_header);
		c->wbytes += body_len;
		set_conn_state(c,conn_write);
		return PROTOCOL_RESP_STATUS_SUCCESS;
	}
	else
	{
		procl_header.header_s.body_len = 0x00;
		memcpy(c->wbuff,procl_header.header_bytes,sizeof(procl_header.header_bytes));
		c->wbytes = sizeof(protocol_header);
		set_conn_state(c,conn_write);
		return PROTOCOL_RESP_STATUS_SUCCESS;
	}
	return status;
}

protocol_resp_status handle_cmd_syncreport(conn *c)
{
	protocol_resp_status status = PROTOCOL_RESP_STATUS_SUCCESS;
	datasevr_syncreport_body_req *req_body;
	datasevr_block *dblk = NULL;
	
	req_body = (datasevr_syncreport_body_req*)c->rcurr;
	dblk = cluster_datasevrblock_find((const char*)req_body->map_info,\
			(size_t)strlen(req_body->map_info),(const void*)req_body->ds_ipaddr,(size_t)strlen(req_body->ds_ipaddr));
	if(dblk == NULL)
	{
		status = PROTOCOL_RESP_STATUS_ERROR_NONEDATASEVERBLOCKS; 
		return status;
	}
	
	dblk->last_sync_sequence = (time_t)buff2long(req_body->last_sync_sequence);
	dblk->last_heartbeat_time = time(NULL);
	dblk->state = active;

	protocol_header *procl_header;
	procl_header = (protocol_header*)c->wbuff;
	procl_header->header_s.body_len = 0x00;
	procl_header->header_s.cmd = (uint8_t)PROTOCOL_CMD_DATASERVER_SYNCREPORT;
	procl_header->header_s.state = (uint8_t)PROTOCOL_RESP_STATUS_SUCCESS;
	c->wbytes = sizeof(protocol_header);
	set_conn_state(c,conn_write);
	return status;
}

protocol_resp_status handle_cmd_statreport(conn *c)
{
	protocol_resp_status status = PROTOCOL_RESP_STATUS_SUCCESS;
	datasevr_statreport_body_req *req_body;
	datasevr_block *dblk = NULL;
	protocol_header *procl_header;
	time_t current_time = time(NULL);
	int old_conns,new_conns;
	wlc_ctx wctx;
	
	req_body = (datasevr_statreport_body_req*)c->rcurr;
	dblk = cluster_datasevrblock_find((const char*)req_body->map_info,\
			(size_t)strlen(req_body->map_info),(const void*)req_body->ds_ipaddr,\
			(size_t)strlen(req_body->ds_ipaddr));
	if(dblk == NULL)
	{
		status = PROTOCOL_RESP_STATUS_ERROR_NONEDATASEVERBLOCKS; 
		return status;
	}
	old_conns = dblk->curr_conns;
	new_conns = (int)buff2long(req_body->conns);
	
	dblk->curr_conns = new_conns;
	dblk->total_mb = (int)buff2long(req_body->total_mb);
	dblk->free_mb = (int)buff2long(req_body->free_mb);
	dblk->last_statreporttimestamp = (time_t)current_time;
	dblk->last_heartbeat_time = (time_t)current_time;
	dblk->state = active;

	wctx.dblk = dblk;
	wctx.old_wlc = old_conns * dblk->weight;
	wctx.new_wlc = new_conns * dblk->weight;
	cluster_wlc_block_add(clusters.wlcsl,&wctx);

	procl_header = (protocol_header*)c->wbuff;
	procl_header->header_s.body_len = 0x00;
	procl_header->header_s.cmd = (uint8_t)PROTOCOL_CMD_DATASERVER_STATREPORT;
	procl_header->header_s.state = (uint8_t)PROTOCOL_RESP_STATUS_SUCCESS;
	c->wbytes = sizeof(protocol_header);

	set_conn_state(c,conn_write);
	return status;
}

protocol_resp_status handle_cmd_test(conn *c)
{
	protocol_resp_status status = PROTOCOL_RESP_STATUS_SUCCESS;
	protocol_header *resp_header;
	resp_header = (protocol_header*)c->wbuff;
	resp_header->header_s.body_len = 0x00;
	resp_header->header_s.cmd = PROTOCOL_CMD_TEST;
	resp_header->header_s.state = PROTOCOL_RESP_STATUS_SUCCESS; 

	c->wbytes = sizeof(resp_header);
	set_conn_state(c,conn_write);
	return status;
}

protocol_resp_status handle_cmd_quited(conn *c)
{
	protocol_resp_status status = PROTOCOL_RESP_STATUS_SUCCESS;
	datasevr_heartbeat_body_req *req_body;
	datasevr_block *dblk = NULL;
	
	req_body = (datasevr_heartbeat_body_req*)c->rcurr;
	dblk = cluster_datasevrblock_find((const char*)req_body->map_info,\
			(size_t)strlen(req_body->map_info),(const void*)req_body->ds_ipaddr,\
			(size_t)strlen(req_body->ds_ipaddr));
	if(dblk == NULL)
	{
		status = PROTOCOL_RESP_STATUS_ERROR; 
		return status;
	}
	dblk->state = off_line;
	set_conn_state(c,conn_closed);
	return status;
}

protocol_resp_status handle_cmd_writetracker(conn *c)
{
	protocol_resp_status status = PROTOCOL_RESP_STATUS_SUCCESS;
	datasevr_block *dblk = NULL;
	lfs_tracker_resp *resp;
	protocol_header *procl_header;

	dblk = cluster_wlc_writedatasevrblock_get();
	if(dblk == NULL)
	{
		return PROTOCOL_RESP_STATUS_ERROR_NO_WRITE_DATASERVER;
	}
	procl_header = (protocol_header*)c->wbuff;
	procl_header->header_s.body_len = sizeof(lfs_tracker_resp);
	procl_header->header_s.cmd = (uint8_t)PROTOCOL_CMD_WRITE_TRACKER_REQ;
	procl_header->header_s.state = (uint8_t)PROTOCOL_RESP_STATUS_SUCCESS;
	c->wbytes = sizeof(protocol_header);
	resp = (lfs_tracker_resp*)(c->wbuff + sizeof(protocol_header));
	memcpy(resp->block_ipaddr,dblk->ip_addr,strlen(dblk->ip_addr));
	long2buff((int64_t)dblk->port,resp->block_port);
	c->wbytes += sizeof(lfs_tracker_resp);
	set_conn_state(c,conn_write);
	return status;
}

protocol_resp_status handle_cmd_readtracker(conn *c)
{
	protocol_resp_status status = PROTOCOL_RESP_STATUS_SUCCESS;
	datasevr_block *dblk = NULL;
	lfs_tracker_resp *resp;
	protocol_header *procl_header;
	lfs_read_tracker_req *req_body;
	int64_t timestamp;

	req_body = (lfs_read_tracker_req*)c->rcurr;
	timestamp = buff2long(req_body->file_timestamp);
	dblk = cluster_readdatasevrblock_get((const char*)req_body->map_info,\
			(size_t)strlen(req_body->map_info),timestamp);
	if(dblk == NULL)
	{
		return PROTOCOL_RESP_STATUS_ERROR_NO_READ_DATASERVER;
	}
	procl_header = (protocol_header*)c->wbuff;
	procl_header->header_s.body_len = sizeof(lfs_tracker_resp);
	procl_header->header_s.cmd = (uint8_t)PROTOCOL_CMD_READ_TRACKER_REQ;
	procl_header->header_s.state = (uint8_t)PROTOCOL_RESP_STATUS_SUCCESS;
	c->wbytes = sizeof(protocol_header);
	resp = (lfs_tracker_resp*)(c->wbuff + sizeof(protocol_header));
	memcpy(resp->block_ipaddr,dblk->ip_addr,strlen(dblk->ip_addr));
	long2buff((int64_t)dblk->port,resp->block_port);
	c->wbytes += sizeof(lfs_tracker_resp);
	set_conn_state(c,conn_write);
	return status;
}

static void write_error_msg(conn *c,protocol_resp_status resp_status)
{
	assert(c != NULL);
	protocol_header *req_header;

	req_header = (protocol_header*)c->rbuff;
	protocol_header procl_header = {
		.header_s.body_len = 0x00,
		.header_s.cmd = (uint8_t)req_header->header_s.cmd,
		.header_s.state = (uint8_t)resp_status
	};
	memcpy(c->wbuff,procl_header.header_bytes,sizeof(procl_header.header_bytes));
	c->wbytes = sizeof(procl_header.header_s);
	set_conn_state(c,conn_write);
	return;
}
