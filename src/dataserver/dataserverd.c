/**
*
*
*
**/
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "logger.h"
#include "hash.h"
#include "hash_table.h"
#include "cfg_file_opt.h"
#include "ds_func.h"
#include "sched_timer.h"
#include "shared_func.h"
#include "ds_conn.h"
#include "ds_network_io.h"
#include "ds_client_network_io.h"
#include "ds_binlog.h"
#include "ds_block.h"
#include "ds_file.h"
#include "ds_sync.h"
#include "ds_disk_io.h"
#include "tracker_client.h"
#include "dataserverd.h"

char base_path[MAX_PATH_SIZE] = {0};
struct setting_st settings;
struct ctx_st ctxs;
struct confs_st confitems;
hash_table cfg_hashtable;
static struct event_base *main_base;
queue_head freecitems;


static void sig_quit_handle(const int sig);
static void sig_ignore_handle(const int sig);
static void thread_sigpipe_register(void);
static void usage(void);
static void usage_license(void);

static void sig_quit_handle(const int sig)
{
	printf("Signal handled: %s.\n",strsignal(sig));
	dataserver_destroy();
	exit(0);
}

static void sig_ignore_handle(const int sig)
{
	printf("Signal: %s is ignored.\n",strsignal(sig));
}

static void thread_sigpipe_register(void)
{
	sigset_t signal_mask;
	sigemptyset(&signal_mask);
	sigaddset(&signal_mask,SIGPIPE);
	int rc = pthread_sigmask(SIG_BLOCK,&signal_mask,NULL);
	if(rc != 0)
	{
		printf("Signal: %s error.\n",strsignal(SIGPIPE));
	}
	return;
}

static void usage(void)
{
	printf("-d  run as a daemon\n"
		"-b     set the backlog queue limit(default:1024)\n"
		"-f		system config file\n"	
		"-t		master or slave server type\n"
		"-v		verbose (print errors/warnings while in event loop)\n"
		"-vv	very verbose (also print client commands/reponses)\n"
		"-vvv	extremely verbose (also print internal state transitions)\n"
		"-h     print this help and exit\n"
		"-i		print memcached and libevent license\n"
		"-V     print version and exit\n"
		);
}

static void usage_license(void)
{
	printf("Copyright (c) 2016, linliwen\n"
			"All rights reserved.\n"
			"\n"
			"Redistribution and use in source and binary forms, with or without\n"
			"modification, are permitted provided that the following conditions are\n"
			"met:\n"
			"\n"
			"    * Redistributions of source code must retain the above copyright\n"
			"notice, this list of conditions and the following disclaimer.\n"
			"\n"
			"    * Redistributions in binary form must reproduce the above\n"
			"copyright notice, this list of conditions and the following disclaimer\n"
			"in the documentation and/or other materials provided with the\n"
			"distribution.\n"
			"\n"
			"    * Neither the name of the linliwen Interactive nor the names of its\n"
			"contributors may be used to endorse or promote products derived from\n"
			"this software without specific prior written permission.\n"
			"\n"
			"THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS\n"
			"\"AS IS\" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT\n"
			"LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR\n"
			"A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT\n"
			"OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,\n"
			"SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT\n"
			"LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,\n"
			"DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY\n"
			"THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT\n"
			"(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE\n"
			"OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.\n"
			);
	return;
}

static void mnt_block_stat_snapshot_cb(int argc,void **argv)
{
	block_stat_flush();
	sched_timer(confitems.block_snapshot_interval * (1000 * 1000),\
			mnt_block_stat_snapshot_cb,NULL);
}

static void block_vfs_statistics_cb(int argc,void **argv)
{
	block_vfs_statistics();
	sched_timer(confitems.block_vfs_interval * (1000 * 1000),\
			block_vfs_statistics_cb,NULL);
}

static void local_binlog_flush_cb(int argc,void **argv)
{
	binlog_lock_flush(&bctx);
	sched_timer(confitems.binlog_flush_interval * (1000 * 1000),\
			local_binlog_flush_cb,NULL);
}

static void remote_binlog_flush_cb(int argc,void **argv)
{
	binlog_lock_flush(&rbctx);
	sched_timer(confitems.binlog_flush_interval * (1000 * 1000),\
			remote_binlog_flush_cb,NULL);
}

int main(int argc,char** argv)
{
	int c;
	int sfd;
	bool do_daemonize = false;
	enum hashfunc_type hash_type = MURMUR3_HASH;


	signal(SIGINT,sig_quit_handle);
	signal(SIGTERM,sig_quit_handle);
	signal(SIGKILL,sig_quit_handle);
	signal(SIGPIPE,sig_ignore_handle);
	thread_sigpipe_register();


	while(-1 != (c = getopt(argc,argv,
		"d" /*daemon mode*/
		"b:" /*listen backlog*/
		"f:" /*conf file path*/
		"hiV" /*help,licence info,version*/
		))){
		switch(c){
		case 'd':
			do_daemonize = true;
			break;
		case 'b':
			settings.backlog = atoi(optarg);
			break;
		case 'f':
			settings.cfgfilepath = optarg;
			break;
		case 'h':
			usage();
			exit(0);
		case 'i':
			usage_license();
			exit(0);
		case 'V':
			printf("lfs\n");
			exit(0);
		default:
			fprintf(stderr,"\nIllegal argument \" %c\"\n",c);
			return 1;
		}
	}
	if(hash_init(hash_type) != 0)
	{
		fprintf(stderr,"\nFailed to initialize hash algorithm!\n");
		exit(0);
	}
	if(cfg_init(&cfg_hashtable,settings.cfgfilepath) != 0)
	{
		fprintf(stderr,"\nFailed to initialize cfg file!\n");
		goto fin;
	}
	/* init settings*/
	settings_init();
	conf_items_init();
	ctxs_init();
	set_cfg2globalobj();
	set_logger_level(confitems.logger_level);
	if(logger_init(base_path,confitems.logger_file_name) != 0)
	{
		fprintf(stderr,"\nFailed to initialize logger!\n");
		goto fin;
	}
	if(conns_init() != 0)
	{
		fprintf(stderr,"\nFailed to initialize connections!\n");
		goto fin;
	}
	if(file_ctx_mpools_init() != 0)
	{
		fprintf(stderr,"\nFailed to file mpools!\n");
		goto fin;
	}
	if(mount_block_map_init() != 0)
	{
		fprintf(stderr,"\nFailed to initialize mount block map!\n");
		goto fin;
	}
	if(binlog_init() != 0)
	{
		fprintf(stderr,"\nFailed to initialize binlog!\n");
		goto fin;
	}
	if(async_thread_init() != 0)
	{
		fprintf(stderr,"\nFailed to initialize async work threads!\n");
		goto fin;
	}

	if(do_daemonize)
	{
		daemon_init(true);
	}
	sched_timer_init();
	main_base = event_init();
	if(nio_threads_init(confitems.nio_threads,main_base) != 0)
	{
		fprintf(stderr,"\nFailed to initialize network work threads!\n");
		goto fin;
	}
	if(dio_thread_pool_init() != 0)
	{
		fprintf(stderr,"\nFailed to initialize disk work threads!\n");
		goto fin;
	}

	sched_timer(confitems.block_snapshot_interval * (1000 * 1000),mnt_block_stat_snapshot_cb,NULL);
	sched_timer(confitems.block_vfs_interval * (1000 * 1000),block_vfs_statistics_cb,NULL);
	sched_timer(confitems.binlog_flush_interval * (1000 * 1000),local_binlog_flush_cb,NULL);
	sched_timer(confitems.binlog_flush_interval * (1000 * 1000),remote_binlog_flush_cb,NULL);

	sfd = server_sockets(confitems.bind_addr,confitems.bind_port,confitems.network_timeout); 
	if(sfd < 0)
	{
		logger_error("Failed to create server socket!");
		goto fin;
	}
	if(trackerclient_info_init() != 0)
	{
		logger_error("Failed to init tracker info!");
		goto fin;
	}
	if(tracker_report_thread_start() != 0)
	{
		logger_error("Failed to startup tracker report thread!");
		goto fin;
	}

	nio_dispatch(sfd);

fin:
	dataserver_destroy();
	return 0;
}
