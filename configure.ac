#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([lfs], [0.1.0], [linliwen@aliyun.com])
AC_CONFIG_SRCDIR([config.h.in])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE

# Checks for programs.
AC_PROG_CC
AC_PROG_RANLIB

AC_ARG_ENABLE(debug, AS_HELP_STRING([--enable-debug], [enable DEBUG mode(default=no)]),[],[enable_debug=no])
	AS_IF([test "x$enable_debug" = "xyes"], [CFLAGS="-g2 -O0 -DDEBUG -Wall"],[test "x$enable_debug" = "xno"],[])

# Checks for libraries.
# FIXME: Replace `main' with a function in `-lpthread':
AC_CHECK_LIB([pthread], [main])
AC_CHECK_LIB([event], [main])

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h limits.h stddef.h stdint.h stdlib.h string.h sys/file.h syslog.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_CHECK_HEADER_STDBOOL
AC_C_INLINE
AC_TYPE_INT8_T
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([localtime_r memset mkdir strchr strerror])

AC_CONFIG_FILES([Makefile
                 src/Makefile
                 src/common/Makefile
				 src/dataserver/Makefile
                 src/trackerserver/Makefile
				 test/Makefile
				 test/common/Makefile
				 test/dataserver/Makefile
				 test/trackerserver/Makefile])
AC_OUTPUT
