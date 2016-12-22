AM_CFLAGS = -I$(top_srcdir)/src/common \
		   -I/usr/local/include	-I$(top_srcdir)/src/include


LDADD = $(top_builddir)/src/common/liblfscommon.a #-L/usr/local/lib 
AM_LDFLAGS = 


DATASERVERD_SOURCE_LIST_HEADER = ds.h ds_func.h ds_queue.h ds_network_io.h lfs_protocol.h \
							  ds_conn.h ds_service.h ds_client_network_io.h tracker_client.h \
							  ds_client_conn.h ds_sync.h ds_block.h ds_file.h ds_types.h ds_disk_io.h \
							ds_binlog.h 


DATASERVERD_SOURCE_LIST = ds_func.c ds_queue.c ds_network_io.c ds_conn.c ds_service.c ds_client_network_io.c \
						  tracker_client.c ds_client_conn.c ds_sync.c ds_block.c ds_file.c ds_disk_io.c \
						  ds_binlog.c

libdataserverd_a_SOURCES = $(DATASERVERD_SOURCE_LIST_HEADER) $(DATASERVERD_SOURCE_LIST)

bin_PROGRAMS = dataserverd
dataserverd_SOURCES = dataserverd.c $(libdataserverd_a_SOURCES) 
