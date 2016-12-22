AM_CFLAGS = -fpic -D ENDIAN_BIG  
AM_LDFLAGS = -lpthread 


noinst_LIBRARIES = liblfscommon.a


liblfscommon_a_SOURCES = shared_func.c shared_func.h common_define.h cfg_file_opt.h cfg_file_opt.c\
						 crc32.c crc32.h logger.c logger.h error.h \
						 jenkins_hash.c jenkins_hash.h murmur3_hash.c murmur3_hash.h hash.c hash.h \
						 hash_table.c hash_table.h pthread_func.h sock_opt.c sock_opt.h \
						 times.h times.c sched_timer.h sched_timer.c base64.c base64.h




include_HEADERS = error.h common_define.h logger.h shared_func.h cfg_file_opt.h crc32.h  \
				  jenkins_hash.h murmur3_hash.h hash.h hash_table.h sock_opt.h \
				  times.h sched_timer.h base64.h
