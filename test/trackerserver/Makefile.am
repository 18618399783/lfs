AM_CFLAGS = -D_GNU_SOURCE -Wformat=0 \
			-I$(top_srcdir)/src/common \
			-I$(top_srcdir)/src/include \
			-I/usr/local/include

AM_LDFLAGS = 

LDADD = $(top_builddir)/src/common/liblfscommon.a


noinst_PROGRAMS = test_trackerd test_write_tracker test_read_tracker
test_trackerd_SOURCES = test_trackerd.c
test_write_tracker_SOURCES = test_write_tracker.c
test_read_tracker_SOURCES = test_read_tracker.c
