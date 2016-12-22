AM_CFLAGS = -D_GNU_SOURCE -Wformat=0 \
			-I$(top_srcdir)/src -I$(top_srcdir)/src/common \
			-I$(top_srcdir)/src/dataserver \
			-I$(top_srcdir)/src/include -I/usr/local/include

AM_LDFLAGS = 

LDADD = $(top_builddir)/src/common/liblfscommon.a


noinst_PROGRAMS = test_upload test_download

test_upload_SOURCES = test_upload.c
test_download_SOURCES = test_download.c
