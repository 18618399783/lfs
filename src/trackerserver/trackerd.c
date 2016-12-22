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
#include <time.h>
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
#include "times.h"
#include "sched_timer.h"
#include "ts_func.h"
#include "shared_func.h"
#include "sock_opt.h"
#include "ts_queue.h"
#include "ts_thread.h"
#include "ts_cluster.h"
#include "ts_wlc.h"
#include "ts_snapshot.h"
#include "trackerd.h"

char base_path[MAX_PATH_SIZE] = {0};
struct setting_st settings;
struct ctx_st ctxs;
struct confs_st confitems;
hash_table cfg_hashtable;
static struct event_base *main_base;
libevent_dispatch_thread dispatch_thread;
queue_head freecitems;
conn **conns;
volatile bool main_thread_flag = true;
volatile time_t system_time = 0;


static void sig_quit_handle(const int sig);
static void sig_ignore_handle(const int sig);
static void thread_sigpipe_register(void);
static void usage(void);
static void usage_license(void);
static void snapshot_schedtimer_cb(int argc,void **argv);

static void sig_quit_handle(const int sig)
{
	printf("Signal handled: %s.\n",strsignal(sig));
	tracker_destroy();
	exit(0);
}

static void sig_ignore_handle(const int sig)
{
	printf("Signal: %s is ignored.\n",strsignal(sig));
	return;
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
		"-v		verbose (print errors/warnings while in event loop)\n"
		"-vv	very verbose (also print client commands/reponses)\n"
		"-vvv	extremely verbose (also print internal state transitions)\n"
		"-h     print this help and exit\n"
		"-i		print memcached and libevent license\n"
		"-V     print version and exit\n"
		);
	return;
}

static void usage_license(void)
{
	printf("Copyright (c) 2016, lanpo Cloud, Inc. <http://www.lanpocloud.com/>\n"
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
			"    * Neither the name of the lanpo cloud Interactive nor the names of its\n"
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

static void snapshot_schedtimer_cb(int argc,void **argv)
{
	do_snapshot();
	sched_timer(confitems.snapshot_thread_interval * (1000 * 1000),snapshot_schedtimer_cb,NULL);
	return;
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

	/* init settings*/
	settings_init();
	ctxs_init();
	conf_items_init();
	conns_init();

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
			printf("lfs 0.0.1\n");
			exit(0);
		default:
			fprintf(stderr,"Illegal argument \" %c\"\n",c);
			return 1;
		}
	}
	if(hash_init(hash_type) != 0)
	{
		fprintf(stderr,"Failed to initialize hash algorithm!\n");
		exit(0);
	}
	if(cfg_init(&cfg_hashtable,settings.cfgfilepath) != 0)
	{
		fprintf(stderr,"Failed to initialize cfg file!\n");
		goto fin;
	}
	if(set_cfg2globalobj() != 0)
	{
		fprintf(stderr,"Failed to initialize cfg to blobal object!\n");
		goto fin;
	}
	set_logger_level(confitems.logger_level);
	if(logger_init(base_path,confitems.logger_file_name) != 0)
	{
		fprintf(stderr,"Failed to initialize logger!\n");
		goto fin;
	}
	if(snapshot_init() != 0)
	{
		fprintf(stderr,"Failed to initialize snapshot work thread!\n");
		goto fin;
	}
	if(cluster_init(0) != 0)
	{
		fprintf(stderr,"Failed to initialize cluster context!\n");
		goto fin;
	}
	if(do_daemonize)
	{
		daemon_init(true);
	}
	sched_timer_init();
	main_base = event_init();

	if(ts_thread_init(confitems.work_threads,main_base) != 0)
	{
		fprintf(stderr,"Failed to initialize trackerd threads!\n");
		goto fin;
	}
	if(cluster_maintenance_thread_start() != 0)
	{
		logger_error("Failed to start cluster maintenance thread.");
		goto fin;
	}
	if(snapshot_thread_start() != 0)
	{
		logger_error("Failed to start snapshot thread.");
		goto fin;
	}
	sfd = server_sockets(confitems.bind_addr,confitems.bind_port,confitems.network_timeout); 
	if(sfd < 0)
	{
		logger_error("Failed to create server socket");
		goto fin;
	}
	sched_timer(confitems.snapshot_thread_interval * (1000 * 1000),snapshot_schedtimer_cb,NULL);

	ts_dispatch(sfd);

fin:
	tracker_destroy();
	return 0;
}
