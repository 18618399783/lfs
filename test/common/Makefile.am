AM_CFLAGS = -D_GNU_SOURCE -Wformat=0 \
			-I$(top_srcdir)/src -I$(top_srcdir)/src/common \
			-I/usr/local/include

AM_LDFLAGS = 

LDADD = $(top_builddir)/src/common/liblfscommon.a

noinst_PROGRAMS = test_hash_table test_str_split
test_hash_table_SOURCES = test_hash_table.c
test_str_split_SOURCES = test_str_split.c
