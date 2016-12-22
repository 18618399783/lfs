AM_CFLAGS = -I$(top_srcdir)/src/common \
		   -I/usr/local/include	-I$(top_srcdir)/src/include


LDADD = $(top_builddir)/src/common/liblfscommon.a #-L/usr/local/lib 
AM_LDFLAGS = 


TRACKERD_SOURCE_LIST_HEADER = trackerd.h ts_func.h ts_queue.h ts_thread.h lfs_protocol.h \
							  ts_conn.h ts_service.h ts_cluster.h ts_datasevr.h ts_types.h \
							  ts_cluster_datasevr.h ts_wlc.h ts_snapshot.h



TRACKERD_SOURCE_LIST = ts_func.c ts_queue.c ts_thread.c ts_conn.c ts_service.c ts_cluster.c \
					   ts_datasevr.c ts_cluster_datasevr.c ts_wlc.c ts_snapshot.c

libtrackerd_a_SOURCES = $(TRACKERD_SOURCE_LIST_HEADER) $(TRACKERD_SOURCE_LIST)

bin_PROGRAMS = trackerd
trackerd_SOURCES = trackerd.c $(libtrackerd_a_SOURCES) 
