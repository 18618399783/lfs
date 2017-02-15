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
#include <errno.h>
#include <assert.h>
#include <stdint.h>
#include <time.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <event.h>
#include <pthread.h>

#include "common_define.h"
#include "lfs_define.h"
#include "lfs_types.h"
#include "lfs_protocol.h"
#include "logger.h"
#include "shared_func.h"
#include "sock_opt.h"
#include "ds_func.h"
#include "ds_types.h"
#include "ds_func.h"
#include "ds_block.h"
#include "ds_client_conn.h"
#include "ds_client_network_io.h"
#include "ds_sync.h"
#include "tracker_client.h"
#include "dataserverd.h"

block_brief volume_blocks[LFS_MAX_BLOCKS_EACH_VOLUME];
int blocks_count = 0;
static pthread_t *tracker_reporter_tids = NULL;
static int tracker_reporter_count = 0;
static pthread_mutex_t tracker_reporter_thread_lock;
volatile int do_run_tracker_reported_thread = 1;
struct tracker_group_st tracker_groups;
static void* __tracker_client_thread_entrance(void *arg);
static void __trackerclient_state_machine(void *arg); 

static int __check_volumeblocks_change(trackerclient_conn *c);
static int __tracker_register(trackerclient_conn *c);
static int __tracker_hearbeat(trackerclient_conn *c);
static int __tracker_report_syncinfo(trackerclient_conn *c);
static int __tracker_report_blockstate(trackerclient_conn *c);
static int __tracker_fullsync_req(trackerclient_conn *c,char *master_ipaddr,int *master_port);
static int __tracker_quit(trackerclient_conn *c);

static block_brief* __local_volume_blocks_find(const char *ipaddr);
static block_brief* __local_volume_blocks_add(block_brief_info_resp *bbirs);
static int __local_volume_blocks_manager(block_brief_info_resp *bbirs,const int bbirs_count);


int tracker_report_thread_start(void)
{
	pthread_attr_t pattr;
	pthread_t tid;
	int ret;
	tracker_info *tis,*tie;

	if((ret = init_pthread_attr(&pattr,confitems.thread_stack_size)) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"Failed init pthread attr: %s", __LINE__,strerror(ret));
		return ret;
	}

	tracker_reporter_tids = (pthread_t*)malloc(sizeof(pthread_t) * tracker_groups.tracker_count);
	if(tracker_reporter_tids == NULL)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"Failed to allocate memery to tracker client thread!errno info: %s", __LINE__,strerror(ret));
		return errno != 0 ? errno : ENOMEM;
	}
	memset(tracker_reporter_tids,0,sizeof(pthread_t) * tracker_groups.tracker_count);
	pthread_mutex_init(&tracker_reporter_thread_lock,NULL);
	tracker_reporter_count = 0; 
	tie = tracker_groups.trackers + tracker_groups.tracker_count;
	for(tis = tracker_groups.trackers;tis < tie; tis++)
	{
		if((ret = pthread_create(&tid,&pattr,__tracker_client_thread_entrance,(void*)tis)) != 0)
		{
			logger_error("file: "__FILE__", line: %d, " \
						"Failed to create tracker report thread! errno info: %s", __LINE__,strerror(ret));
			return ret;
		}
		pthread_mutex_lock(&tracker_reporter_thread_lock);
		tracker_reporter_tids[tracker_reporter_count] = tid;
		tracker_reporter_count++;
		pthread_mutex_unlock(&tracker_reporter_thread_lock);
	}
	pthread_attr_destroy(&pattr);
	return LFS_OK;	
}

static void* __tracker_client_thread_entrance(void *arg)
{
	trackerclient_conn *c = NULL;
	tracker_info *ti = (tracker_info*)arg;

	c = trackerclientconn_new();
	if(NULL == c)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"Failed to allocate tracker client connection!", __LINE__);
		return NULL;
	}
	c->index = ti->index;
	set_trackerclientconn_state(c,connectd);

	__trackerclient_state_machine((void*)c);	
	return NULL;
}

static void __trackerclient_state_machine(void *arg)
{
	int failed_connect_count = 0;
	time_t current_time;
	bool is_local_haseregistered = false;
	bool is_local_hasefullsync = false;
	trackerclient_conn *c = (trackerclient_conn*)arg;

	while(do_run_tracker_reported_thread && tracker_reporter_count < tracker_groups.tracker_count)
	{
		sleep(1);
	}

	while(do_run_tracker_reported_thread)
	{
		switch(c->state)
		{
		case connectd:
			c->sfd = socket(AF_INET,SOCK_STREAM,0);
			if(c->sfd < 0)
			{
				logger_error("file: "__FILE__", line: %d, " \
						"Failed to create socket,errno:%d," \
						"error info:%s!", __LINE__,errno,strerror(errno));
				do_run_tracker_reported_thread = false;
				break;
			}
			if(socket_bind(c->sfd,NULL,0) != 0)
				set_trackerclientconn_state(c,closed);
			if(set_noblock(c->sfd) != 0)
				set_trackerclientconn_state(c,closed);
			if(set_sock_opt(c->sfd,confitems.network_timeout) != 0)
				set_trackerclientconn_state(c,closed);
			if(connectserver_nb(c->sfd,tracker_groups.trackers[c->index].ip_addr,tracker_groups.trackers[c->index].port,confitems.network_timeout) != 0)
			{
				logger_error("file: "__FILE__", line: %d, " \
						"Failed to connect the tracker server %s:%d,errno:%d,"\
						"error info:%s!",\
						__LINE__,tracker_groups.trackers[c->index].ip_addr,tracker_groups.trackers[c->index].port,errno,strerror(errno));
				failed_connect_count++;
				if(do_run_tracker_reported_thread)
				{
					sleep(confitems.heart_beat_interval);
					set_trackerclientconn_state(c,closed);
				}
				else
				{
					return;
				}
			}
			else
			{
				CTXS_LOCK();
				ctxs.curr_conns++;
				ctxs.total_conns++;
				CTXS_UNLOCK();
				logger_info("file: "__FILE__", line: %d, " \
						"successfully connect to tracker server %s:%d!" \
						, __LINE__,tracker_groups.trackers[c->index].ip_addr,tracker_groups.trackers[c->index].port);
				set_trackerclientconn_state(c,registerd);
			}
			break;
		case registerd:
			do
			{
				if(!is_local_haseregistered)
				{
					if(__tracker_register(c) != 0)
					{
						logger_error("file: "__FILE__", line: %d, " \
								"Server(%s:%d) register to tracker server(%s:%d) failed!", \
								__LINE__,\
								confitems.bind_addr,\
								confitems.bind_port,\
								tracker_groups.trackers[c->index].ip_addr,\
								tracker_groups.trackers[c->index].port);
						set_trackerclientconn_state(c,closed);
						break;
					}
					else
					{
						logger_info("Server(%s:%d) register to tracker server(%s:%d) success.",\
								confitems.bind_addr,\
								confitems.bind_port,\
								tracker_groups.trackers[c->index].ip_addr,\
								tracker_groups.trackers[c->index].port);
						is_local_haseregistered = true;
						ctxs.register_timestamp = (int)time(NULL);
						set_trackerclientconn_state(c,fullsync);
					}
				}
				else
				{
					logger_info("Server(%s:%d) hase register to tracker server(%s:%d).",\
							confitems.bind_addr,\
							confitems.bind_port,\
							tracker_groups.trackers[c->index].ip_addr,\
							tracker_groups.trackers[c->index].port);
					set_trackerclientconn_state(c,fullsync);
					break;
				}
			}while(0);
			break;
		case fullsync:
			do
			{
				int ret = LFS_OK;
				connect_info cinfo;
				memset(&cinfo,0,sizeof(connect_info));
				if(!is_local_hasefullsync)
				{
					if((ret = pthread_mutex_lock(&tracker_reporter_thread_lock)))
					{

						logger_error("file: "__FILE__", line: %d, " \
								"Call tracker report thread mutex failed," \
								"errno:%d,error info: %s", __LINE__,ret,strerror(ret));
						set_trackerclientconn_state(c,quited);
						break;
					}

					if(!ctxs.is_fullsyncdone)
					{
						if((ret = __tracker_fullsync_req(c,cinfo.ipaddr,&cinfo.port)) != 0)
						{
							logger_error("file: "__FILE__", line: %d, " \
									"Full sync request the master server failed.",\
								   	__LINE__);
							pthread_mutex_unlock(&tracker_reporter_thread_lock);
							set_trackerclientconn_state(c,c->nextto);
#ifdef _DEBUG_
							logger_info("Server(%s:%d) sync setting request to the tracker(%s:%d) failed,will closed socket and reconnected.",\
									confitems.bind_addr,\
									confitems.bind_port,\
									tracker_groups.trackers[c->index].ip_addr,\
									tracker_groups.trackers[c->index].port);
#endif
							break;
						}
						if(is_local_block((const char*)cinfo.ipaddr))
						{
							logger_info("Request sync data of the host(%s:%d) is the local machine,so donn't need the full sync.",\
									confitems.bind_addr,\
									confitems.bind_port);
							is_local_hasefullsync = true;
							pthread_mutex_unlock(&tracker_reporter_thread_lock);
							set_trackerclientconn_state(c,reporting);
							break;
						}
						enum full_sync_state fstate;
						fstate = full_sync_from_master(&cinfo);
						if(fstate == F_SYNC_NETWORK_ERROR)
						{
							logger_error("file: "__FILE__", line: %d, " \
									"Connection is not on the master(%s:%d) server,out of full sync.",\
								   	__LINE__,cinfo.ipaddr,cinfo.port);
							do_run_tracker_reported_thread = false;
							set_trackerclientconn_state(c,quited);
							pthread_mutex_unlock(&tracker_reporter_thread_lock);
							break;
						}
						else if(fstate == F_SYNC_ERROR)
						{
							logger_error("file: "__FILE__", line: %d, " \
									"Full sync data from master(%s:%d) server failed.",\
								   	__LINE__,cinfo.ipaddr,cinfo.port);
							do_run_tracker_reported_thread = false;
							set_trackerclientconn_state(c,quited);
							pthread_mutex_unlock(&tracker_reporter_thread_lock);
							break;
						}
						else if(fstate == F_SYNC_OK)
						{
							logger_error("file: "__FILE__", line: %d, " \
									"From the master(%s:%d) server full " \
									"sync data unfinished.",\
								   	__LINE__,cinfo.ipaddr,cinfo.port);
							do_run_tracker_reported_thread = false;
							set_trackerclientconn_state(c,quited);
							pthread_mutex_unlock(&tracker_reporter_thread_lock);
							break;
						}
						else if(fstate == F_SYNC_FINISH)
						{
							ctxs.is_fullsyncdone = true;
							if(ctx_snapshot_batdata_flush() != 0)
							{
								logger_error("file: "__FILE__", line: %d, " \
										"Write full sync ini data failed.",\
										__LINE__);
								do_run_tracker_reported_thread = false;
								pthread_mutex_unlock(&tracker_reporter_thread_lock);
								break;
							}
							logger_info("Success full sync data from the master(%s:%d).",cinfo.ipaddr,cinfo.port);
						}
					}
					is_local_hasefullsync = true;
					pthread_mutex_unlock(&tracker_reporter_thread_lock);
					set_trackerclientconn_state(c,reporting);
					break;
				}
				else
				{
					logger_info("Server(%s:%d) will sync reporting to tracker server(%s:%d).",\
							confitems.bind_addr,\
							confitems.bind_port,\
							tracker_groups.trackers[c->index].ip_addr,\
							tracker_groups.trackers[c->index].port);
					set_trackerclientconn_state(c,reporting);
					break;
				}
			}while(0);
			break;
		case reporting:
			while(do_run_tracker_reported_thread)
			{
				current_time = time(NULL);
				if(current_time - c->last_heartbeat_time \
					   	>= confitems.heart_beat_interval)
				{
					if(__tracker_hearbeat(c) != 0)
					{
						set_trackerclientconn_state(c,closed);
						break;
					}
					c->last_heartbeat_time = current_time;
				}

				if(current_time - c->last_syncreport_time \
					   	>= confitems.sync_report_interval)
				{
					if(__tracker_report_syncinfo(c) != 0)
					{
						set_trackerclientconn_state(c,quited);
						break;
					}
					c->last_heartbeat_time = current_time;
					c->last_syncreport_time = current_time;
				}

				if(current_time - c->last_reportstate_time \
							>= confitems.state_report_interval)
				{
					if(__tracker_report_blockstate(c) != 0)
					{
						set_trackerclientconn_state(c,closed);
						break;
					}
					c->last_heartbeat_time = current_time;
					c->last_reportstate_time = current_time;
				}
				sleep(1);
			}
			break;
		case quited:
			// quit tracker manager
			if((!do_run_tracker_reported_thread) && \
					(__tracker_quit(c) != 0))
			{
			}
			sleep(1);
			trackerclientconn_close(c);
			//set_trackerclientconn_state(c,connectd);
			break;
		case closed:
			sleep(confitems.heart_beat_interval);
			trackerclientconn_close(c);
			set_trackerclientconn_state(c,connectd);
			break;
		}
	}
}

int trackerclient_info_init()
{
	int ret = LFS_OK;
	int tracker_count = 0;
	int in = 0,index = 0;
	const char de[2] = "|";
	const char di[2] = ":";
	const char sep = '|';
	char *buff = NULL;
	char *outer_ptr = NULL;
	char *inner_ptr = NULL;
	char *p[32];

	buff = confitems.tracker_servers;
	
	assert(buff != NULL);
	memset(&tracker_groups,0,sizeof(tracker_groups));

	tracker_count = get_split_str_count(buff,sep); 
	tracker_groups.tracker_count = tracker_count;
	tracker_groups.trackers = (tracker_info*)malloc(sizeof(tracker_info) * tracker_count);
	if(tracker_groups.trackers == NULL)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"Failed to allocate memery to trackers group!", __LINE__);
		return LFS_ERROR;
	}
	memset(tracker_groups.trackers,0,sizeof(tracker_info) * tracker_count);

	while((p[in] = strtok_r(buff,de,&outer_ptr)) != NULL)
	{
		buff = p[in];
	   while((p[in] = strtok_r(buff,di,&inner_ptr)) != NULL)	
	   {
		   if((in % 2) == 0)
		   {
			   memcpy(tracker_groups.trackers[index].ip_addr,p[in],strlen(p[in]));
		   }
		   else if((in % 2) == 1)
		   {
			   tracker_groups.trackers[index].port = atoi(p[in]);
		   }
		   in++;
		   buff = NULL;
	   }
	   tracker_groups.trackers[index].index = index;
	   index++;
	   buff = NULL;
	}
	return ret;
}

#if 0
static int __check_masterblock_change(trackerclient_conn *c)
{
	int ret = LFS_OK;
	int64_t resp_bytes = 0;
	char resp_buff[sizeof(datasevr_masterblock_body_resp)] = {0};
	datasevr_masterblock_body_resp *resp_body;

	if((ret = client_recvheader(c->sfd,&resp_bytes)) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Server(%s:%d) received master block info data from tracker server(%s:%d) failed,errno:%02X.", \
				__LINE__,\
				confitems.bind_addr,\
				confitems.bind_port,\
				tracker_groups.trackers[c->index].ip_addr,\
				tracker_groups.trackers[c->index].port,ret);
		c->nextto = quited;
		return ret;
	}
	if(resp_bytes == 0)
	{
		logger_warning("file: "__FILE__", line: %d, " \
				"Server(%s:%d) have not received any master block infomation from tracker server(%s:%d).", \
				__LINE__,\
				confitems.bind_addr,\
				confitems.bind_port,\
				tracker_groups.trackers[c->index].ip_addr,\
				tracker_groups.trackers[c->index].port);
		return LFS_OK;
	}
	if((ret = client_recvdata_nomalloc(c->sfd,(char*)resp_buff,(const int64_t)resp_bytes)) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Server(%s:%d) received master block info data from tracker server(%s:%d) failed.", \
				__LINE__,\
				confitems.bind_addr,\
				confitems.bind_port,\
				tracker_groups.trackers[c->index].ip_addr,\
				tracker_groups.trackers[c->index].port);
		return ret;
	}
	resp_body = (datasevr_masterblock_body_resp*)resp_buff;
	return ret;
}
#endif

static int __check_volumeblocks_change(trackerclient_conn *c)
{
	int ret = LFS_OK;
	int bcount = 0;
	int64_t resp_bytes = 0;
	char resp_buff[sizeof(block_brief_info_resp) * LFS_MAX_BLOCKS_EACH_VOLUME];

	if((ret = client_recvheader(c->sfd,&resp_bytes)) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Server(%s:%d) received sync report response package data from tracker server(%s:%d) failed,errno:%02x.", \
				__LINE__,\
				confitems.bind_addr,\
				confitems.bind_port,\
				tracker_groups.trackers[c->index].ip_addr,\
				tracker_groups.trackers[c->index].port,ret);
		c->nextto = closed;
		return ret;
	}
	if(resp_bytes == 0)
	{
		return LFS_OK;
	}
	if((ret = client_recvdata_nomalloc(c->sfd,(char*)resp_buff,(const int64_t)resp_bytes)) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Server(%s:%d) received sync report response package data from tracker server(%s:%d) error!", \
				__LINE__,\
				confitems.bind_addr,\
				confitems.bind_port,\
				tracker_groups.trackers[c->index].ip_addr,\
				tracker_groups.trackers[c->index].port);
		return ret;
	}
	bcount = resp_bytes / sizeof(block_brief_info_resp);
   if(bcount > LFS_MAX_BLOCKS_EACH_VOLUME)	
   {
		logger_error("file: "__FILE__", line: %d, " \
				"Server(%s:%d) received %d volume block from tracker server(%s:%d),exceed max %d or zero.", \
				__LINE__,\
				confitems.bind_addr,\
				confitems.bind_port,\
				bcount,\
				tracker_groups.trackers[c->index].ip_addr,\
				tracker_groups.trackers[c->index].port,\
				LFS_MAX_BLOCKS_EACH_VOLUME);
		return LFS_ERROR;
   }
   ret = __local_volume_blocks_manager((block_brief_info_resp*)resp_buff,bcount);
	return ret; 
}

static int __tracker_register(trackerclient_conn *c)
{
	int ret = LFS_OK;
	time_t current_time = time(NULL);
	int64_t resp_bytes = 0;
	protocol_header *req_header;
	datasevr_reg_body_req *req_body;
	char req_buff[sizeof(protocol_header) + sizeof(datasevr_reg_body_req)] = {0};

	memset(req_buff,0,sizeof(req_buff));
	req_header = (protocol_header*)req_buff;
	req_body = (datasevr_reg_body_req*)(req_buff + sizeof(protocol_header));

	req_header->header_s.body_len = sizeof(datasevr_reg_body_req);
	req_header->header_s.cmd = PROTOCOL_CMD_DATASERVER_REGISTER;

	sprintf(req_body->map_info,"%s/%s",confitems.group_name,confitems.volume_name);
	strcpy(req_body->ds_ipaddr,confitems.bind_addr);
	long2buff((int64_t)confitems.bind_port,req_body->ds_port);
	long2buff((int64_t)confitems.weight,req_body->weight);
	long2buff((int64_t)confitems.heart_beat_interval,req_body->heart_beat_interval);
	long2buff((int64_t)current_time,req_body->reg_time);
	long2buff((int64_t)ctxs.started_time,req_body->started_time);


	if((ret = client_senddata(c->sfd,req_buff,sizeof(req_buff))) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"Server(%s:%d) write register package data to tracker server(%s:%d) failed!", \
				   	__LINE__,\
					confitems.bind_addr,\
					confitems.bind_port,\
					tracker_groups.trackers[c->index].ip_addr,\
					tracker_groups.trackers[c->index].port);
		return ret;
	}

	if((ret = client_recvheader(c->sfd,&resp_bytes)) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"Server(%s:%d) read register response from tracker server(%s:%d) failed!", \
				   	__LINE__,\
					confitems.bind_addr,\
					confitems.bind_port,\
					tracker_groups.trackers[c->index].ip_addr,\
					tracker_groups.trackers[c->index].port);
		return ret;
	}
	return ret;
}

static int __tracker_hearbeat(trackerclient_conn *c)
{
	int ret = LFS_OK;
	protocol_header *req_header;
	datasevr_heartbeat_body_req *req_body;
	char req_buff[sizeof(protocol_header) + sizeof(datasevr_heartbeat_body_req)];

	memset(req_buff,0,sizeof(req_buff));
	req_header = (protocol_header*)req_buff;
	req_body = (datasevr_heartbeat_body_req*)(req_buff + sizeof(protocol_header));

	req_header->header_s.body_len = sizeof(datasevr_heartbeat_body_req);
	req_header->header_s.cmd = PROTOCOL_CMD_DATASERVER_HEARTBEAT;

	sprintf(req_body->map_info,"%s/%s",confitems.group_name,confitems.volume_name);
	strcpy(req_body->ds_ipaddr,confitems.bind_addr);

	if((ret = client_senddata(c->sfd,req_buff,sizeof(req_buff))) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"Server(%s:%d) write heartbeat package data to tracker server(%s:%d) failed!", \
				   	__LINE__,\
					confitems.bind_addr,\
					confitems.bind_port,\
					tracker_groups.trackers[c->index].ip_addr,\
					tracker_groups.trackers[c->index].port);
		return ret;
	}

#ifdef _DEBUG_
	logger_info("Server(%s:%d) report heartbeat package to the server(%s:%d) success,status %d.",\
			confitems.bind_addr,\
			confitems.bind_port,\
			tracker_groups.trackers[c->index].ip_addr,\
			tracker_groups.trackers[c->index].port,\
			ret);
#endif
	return __check_volumeblocks_change(c);
}

static int __tracker_report_syncinfo(trackerclient_conn *c)
{
	int ret = LFS_OK;
	int64_t resp_bytes;
	protocol_header *req_header;
	datasevr_syncreport_body_req *req_body;
	char req_buff[sizeof(protocol_header) + sizeof(datasevr_syncreport_body_req)];

	memset(req_buff,0,sizeof(req_buff));
	req_header = (protocol_header*)req_buff;
	req_body = (datasevr_syncreport_body_req*)(req_buff + sizeof(protocol_header));

	req_header->header_s.body_len = sizeof(datasevr_syncreport_body_req);
	req_header->header_s.cmd = PROTOCOL_CMD_DATASERVER_SYNCREPORT;

	sprintf(req_body->map_info,"%s/%s",confitems.group_name,confitems.volume_name);
	strcpy(req_body->ds_ipaddr,confitems.bind_addr);
	long2buff(ctxs.last_sync_sequence,req_body->last_sync_sequence);

	if((ret = client_senddata(c->sfd,req_buff,sizeof(req_buff))) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"Server(%s:%d) send sync message package data to tracker server(%s:%d) failed!", \
				   	__LINE__,\
					confitems.bind_addr,\
					confitems.bind_port,\
					tracker_groups.trackers[c->index].ip_addr,\
					tracker_groups.trackers[c->index].port);
		return ret;
	}
	if((ret = client_recvheader(c->sfd,&resp_bytes)) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"Server(%s:%d) receive sync message package data to tracker server(%s:%d) failed!", \
				   	__LINE__,\
					confitems.bind_addr,\
					confitems.bind_port,\
					tracker_groups.trackers[c->index].ip_addr,\
					tracker_groups.trackers[c->index].port);
		return ret;
	}

#ifdef _DEBUG_
	logger_info("Server(%s:%d) report sync info to the server(%s:%d) success,status %d.",\
			confitems.bind_addr,\
			confitems.bind_port,\
			tracker_groups.trackers[c->index].ip_addr,\
			tracker_groups.trackers[c->index].port,\
			ret);
#endif
	return ret;
}

static int __tracker_report_blockstate(trackerclient_conn *c)
{
	int ret = LFS_OK;
	int64_t resp_bytes;
	protocol_header *req_header;
	datasevr_statreport_body_req *req_body;
	char req_buff[sizeof(protocol_header) + sizeof(datasevr_statreport_body_req)];

	memset(req_buff,0,sizeof(req_buff));
	req_header = (protocol_header*)req_buff;
	req_body = (datasevr_statreport_body_req*)(req_buff + sizeof(protocol_header));

	req_header->header_s.body_len = sizeof(datasevr_statreport_body_req);
	req_header->header_s.cmd = PROTOCOL_CMD_DATASERVER_STATREPORT;

	sprintf(req_body->map_info,"%s/%s",confitems.group_name,confitems.volume_name);
	strcpy(req_body->ds_ipaddr,confitems.bind_addr);
	long2buff((int64_t)ctxs.curr_conns,req_body->conns);
	long2buff((int64_t)mounts.block_total_size_mb,req_body->total_mb);
	long2buff((int64_t)mounts.block_free_size_mb,req_body->free_mb);

	if((ret = client_senddata(c->sfd,req_buff,sizeof(req_buff))) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"Server(%s:%d) write block stat package data to tracker server(%s:%d) failed!", \
				   	__LINE__,\
					confitems.bind_addr,\
					confitems.bind_port,\
					tracker_groups.trackers[c->index].ip_addr,\
					tracker_groups.trackers[c->index].port);
		return ret;
	}

	if((ret = client_recvheader(c->sfd,&resp_bytes)) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
				"Server(%s:%d) recvived block stat response package data from tracker server(%s:%d) failed!", \
				__LINE__,\
				confitems.bind_addr,\
				confitems.bind_port,\
				tracker_groups.trackers[c->index].ip_addr,\
				tracker_groups.trackers[c->index].port);
		return ret;
	}
#ifdef _DEBUG_
	logger_info("Server(%s:%d) report stat to tracker server(%s:%d) report success,status %d.",\
			confitems.bind_addr,\
			confitems.bind_port,\
			tracker_groups.trackers[c->index].ip_addr,\
			tracker_groups.trackers[c->index].port,\
			ret);
#endif
	return ret;
}

static int __tracker_fullsync_req(trackerclient_conn *c,char *master_ipaddr,int *master_port)
{
	int ret = LFS_OK;
	int64_t resp_bytes = 0;
	protocol_header *req_header;
	datasevr_fullsyncreq_body_req *req_body;
	char *resp_buff = NULL;
	datasevr_masterblock_body_resp *resp_body;
	char req_buff[sizeof(protocol_header) + sizeof(datasevr_fullsyncreq_body_req)];

	memset(req_buff,0,sizeof(req_buff));
	req_header = (protocol_header*)req_buff;
	req_body = (datasevr_fullsyncreq_body_req*)(req_buff + sizeof(protocol_header));

	req_header->header_s.body_len = sizeof(datasevr_fullsyncreq_body_req);
	req_header->header_s.cmd = PROTOCOL_CMD_DATASERVER_FULLSYNC;

	snprintf(req_body->map_info,sizeof(req_body->map_info),"%s/%s",confitems.group_name,confitems.volume_name);
	strcpy(req_body->ds_ipaddr,confitems.bind_addr);


	if((ret = client_senddata(c->sfd,req_buff,sizeof(req_buff))) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"Server(%s:%d) write full sync package data to tracker server(%s:%d) failed!", \
				   	__LINE__,\
					confitems.bind_addr,\
					confitems.bind_port,\
					tracker_groups.trackers[c->index].ip_addr,\
					tracker_groups.trackers[c->index].port);
		c->nextto = closed;
		return ret;
	}

	if((ret = client_recvdata(c->sfd,&resp_buff,&resp_bytes)) != 0)
	{
		switch(ret)
		{
			case PROTOCOL_RESP_STATUS_ERROR_UNREGISTER:
				logger_warning("The tracker server %s:%d is not register.",\
						tracker_groups.trackers[c->index].ip_addr,\
						tracker_groups.trackers[c->index].port,c->sfd);
				c->nextto = registerd;
				break;
			case PROTOCOL_RESP_STATUS_SYNCSETTING_WAITING:
				logger_warning("The tracker server %s:%d volume belongs to no master server.",\
						tracker_groups.trackers[c->index].ip_addr,\
						tracker_groups.trackers[c->index].port,c->sfd);
				c->nextto = fullsync;
				sleep(confitems.heart_beat_interval);
				break;
			default:
				logger_error("file: "__FILE__", line: %d, " \
						"Server(%s:%d) read full sync response package data from tracker server(%s:%d) failed!", \
						__LINE__,\
						confitems.bind_addr,\
						confitems.bind_port,\
						tracker_groups.trackers[c->index].ip_addr,\
						tracker_groups.trackers[c->index].port);
				c->nextto = quited;
				break;

		}
		return ret;
	}
	if(resp_buff == NULL)
	{
		logger_error("file: "__FILE__", line: %d, "\
					"Server(%s:%d) read full sync response package data from tracker server(%s:%d) is NULL!", \
				   	__LINE__,\
					confitems.bind_addr,\
					confitems.bind_port,\
					tracker_groups.trackers[c->index].ip_addr,\
					tracker_groups.trackers[c->index].port);
		c->nextto = closed;
		return -2;
	}
	resp_body = (datasevr_masterblock_body_resp*)resp_buff;
	strcpy(master_ipaddr,resp_body->master_ipaddr);
	*master_port = (int)buff2long(resp_body->master_port);
#ifdef _DEBUG_
	logger_info("Server(%s:%d) fullsync to the server(%s:%d) success,status %d.",\
			confitems.bind_addr,\
			confitems.bind_port,\
			tracker_groups.trackers[c->index].ip_addr,\
			tracker_groups.trackers[c->index].port,\
			ret);
#endif
	return ret;
}

static int __tracker_quit(trackerclient_conn *c)
{
	int ret = LFS_OK;
	protocol_header *req_header;
	datasevr_quited_body_req *req_body;
	char req_buff[sizeof(protocol_header) + sizeof(datasevr_quited_body_req)];

	memset(req_buff,0,sizeof(req_buff));
	req_header = (protocol_header*)req_buff;
	req_body = (datasevr_quited_body_req*)(req_buff + sizeof(protocol_header));

	req_header->header_s.body_len = sizeof(datasevr_quited_body_req);
	req_header->header_s.cmd = PROTOCOL_CMD_DATASERVER_QUITED;

	sprintf(req_body->map_info,"%s/%s",confitems.group_name,confitems.volume_name);
	strcpy(req_body->ds_ipaddr,confitems.bind_addr);

	if((ret = client_senddata(c->sfd,req_buff,sizeof(req_buff))) != 0)
	{
		logger_error("file: "__FILE__", line: %d, " \
					"Server(%s:%d) write quited server package data to tracker server(%s:%d) failed!", \
				   	__LINE__,\
					confitems.bind_addr,\
					confitems.bind_port,\
					tracker_groups.trackers[c->index].ip_addr,\
					tracker_groups.trackers[c->index].port);
		return ret;
	}

#ifdef _DEBUG_
	logger_info("Server(%s:%d) quit server the server(%s:%d) success.",\
			confitems.bind_addr,\
			confitems.bind_port,\
			tracker_groups.trackers[c->index].ip_addr,\
			tracker_groups.trackers[c->index].port);
#endif
	return ret;
}

static block_brief* __local_volume_blocks_find(const char *ipaddr)
{
	int i;
	for(i = 0; i < blocks_count; i++)
	{
		if(strcmp(ipaddr,volume_blocks[i].ipaddr) == 0)
		{
			return &volume_blocks[i];
		}
	}
	return NULL;
}

static block_brief* __local_volume_blocks_add(block_brief_info_resp *bbirs)
{
	assert(bbirs != NULL);
	int block_index;

	pthread_mutex_lock(&tracker_reporter_thread_lock);
	block_index = blocks_count;
	blocks_count++;
	pthread_mutex_unlock(&tracker_reporter_thread_lock);
	volume_blocks[block_index].state = (int)buff2long(bbirs->state);
	volume_blocks[block_index].port = (int)buff2long(bbirs->ds_port);
	volume_blocks[block_index].last_sync_sequence = buff2long(bbirs->last_sync_sequence);
	memcpy(volume_blocks[block_index].ipaddr,bbirs->ds_ipaddr,strlen(bbirs->ds_ipaddr));
	return &volume_blocks[block_index];
}

static int __local_volume_blocks_manager(block_brief_info_resp *bbirs,const int bbirs_count)
{
	assert(bbirs != NULL);
	assert(bbirs_count != 0);
	int ret;
	block_brief_info_resp *ites;
	block_brief_info_resp *itee;
	block_brief *pf = NULL;
	block_brief *pi = NULL;
	enum datasevr_state new_state;

	ites = bbirs;
	itee = bbirs + bbirs_count;
	for(; ites < itee; ites++)
	{
		pf = __local_volume_blocks_find((const char*)ites->ds_ipaddr);
		if(pf != NULL)
		{
			new_state = (int)buff2long(ites->state);
			pf->last_sync_sequence = buff2long(ites->last_sync_sequence);
			if(pf->state != new_state)
			{
				if(((pf->state == init) || \
							(pf->state == wait_sync) || \
							(pf->state == syncing) || \
							(pf->state == off_line)) && \
						((new_state == on_line) || \
						 (new_state == active)))
				{
					pf->state = new_state;
					if((ret = async_thread_start(pf)) != 0)
					{
						logger_error("file: "__FILE__", line: %d, " \
								"Dataserver(%s:%d) started async thread to server(%s:%d) failed!", \
								__LINE__,\
								confitems.bind_addr,\
								confitems.bind_port,\
								ites->ds_ipaddr,\
								ites->ds_port);
						continue;
					}
				}
			}
		}
		else
		{
			pi = __local_volume_blocks_add(ites);
			if(pi == NULL)
			{
				logger_error("file: "__FILE__", line: %d, " \
						"Remote server(%s:%d) insert to local server(%s:%d) volume manager failed!", \
						__LINE__,\
						ites->ds_ipaddr,\
						ites->ds_port,\
						confitems.bind_addr,\
						confitems.bind_port);
				continue;
			}
			if((ret = async_thread_start(pi)) != 0)
			{
				logger_error("file: "__FILE__", line: %d, " \
						"Dataserver(%s:%d) started async thread to slave server(%s:%d) failed!", \
						__LINE__,\
						confitems.bind_addr,\
						confitems.bind_port,\
						ites->ds_ipaddr,\
						ites->ds_port);
				continue;
			}
		}
	}
	return LFS_OK; 
}
