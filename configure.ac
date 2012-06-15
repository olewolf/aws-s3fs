# Copyright (C) 2012 Ole Wolf <wolf@blazingangles.com>
#
# This file is part of aws-s3fs.
# 
# aws-s3fs is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.


AC_INIT([aws-s3fs], [0.1], [wolf@blazingangles.com])
AC_CONFIG_SRCDIR([src/aws-s3fs.c])
AC_CONFIG_AUX_DIR([config])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE

# Prepare autotest suite.
AC_CONFIG_TESTDIR([test])
AC_CONFIG_FILES([test/atlocal])

# Add configuration options.
AC_ARG_ENABLE([doc],
    AS_HELP_STRING([--enable-doc], [Build source code documentation]),
	[ac_enable_doc=$enableval], [ac_enable_doc=auto])
AC_ARG_ENABLE([openssltest],
    AS_HELP_STRING([--disable-openssltest], [Skip OpenSSL tests]),
	[ac_enable_openssltest=$enableval], [ac_enable_openssltest=auto])

# Check for programs.
AC_PROG_CC
AC_PROG_CC_STDC
AM_PROG_CC_C_O
AC_PROG_INSTALL
AC_PROG_MAKE_SET
AC_CHECK_PROG(HAVE_DOXYGEN, doxygen, true, false)
AC_CHECK_PROG(HAVE_GRAPHVIZ, dot, true, false)
AC_CHECK_PROG(HAVE_OPENSSL, openssl, true, false)
AC_PATH_PROGS(DOXYGEN, doxygen)
#AC_PATH_PROGS(SPLINT, splint)
#AC_PATH_PROGS(FLAWFINDER, flawfinder)
#AC_PATH_PROGS(RATS, rats)
#AC_PATH_PROGS(PSCAN, pscan)
#AC_PATH_PROGS(GRAPHVIZ, dot)

AC_C_CONST
AC_C_INLINE

# Check for libraries.
AC_CHECK_LIB([config], [config_init],,
    AC_MSG_ERROR([*** config library not found **]))
AC_CHECK_LIB([fuse], [fuse_main],,
    AC_MSG_ERROR([*** fuse library not found **]))
AC_CHECK_LIB([pthread], [pthread_mutex_lock],,
    AC_MSG_ERROR([*** pthread library function not found **]))
AC_CHECK_LIB([curl], [curl_easy_init],,
    AC_MSG_ERROR([*** curl library not found **]))
AC_CHECK_LIB([rest-0.7], [rest_param_get_name],,
    AC_MSG_ERROR([*** rest library not found **]))

# Check for modules.
PKG_CHECK_MODULES([DEPS], [fuse >= 2.8 libconfig >= 1.3 libcurl >= 7.20 \
                  rest-0.7 >= 0.7],,
    AC_MSG_ERROR([*** libfuse, libconfig, librest, or curl source not found **]))

# Check for header files.
AC_CHECK_HEADERS([stdio.h],,
    AC_MSG_ERROR([*** stdio.h missing **]))
AC_CHECK_HEADERS([string.h],,
    AC_MSG_ERROR([*** string.h missing **]))
AC_CHECK_HEADERS([stdlib.h],,
    AC_MSG_ERROR([*** stdlib.h not found **]))
AC_CHECK_HEADERS([stdbool.h],,
    AC_MSG_ERROR([*** stdbool.h not found **]))
AC_CHECK_HEADERS([libconfig.h],,
    AC_MSG_ERROR([*** libconfig.h not found **]))
AC_CHECK_HEADERS([getopt.h],,
    AC_MSG_ERROR([*** getopt.h not found **]))
AC_CHECK_HEADERS([assert.h],,
    AC_MSG_ERROR([*** assert.h not found **]))
AC_CHECK_HEADERS([ctype.h],,
    AC_MSG_ERROR([*** ctype.h not found **]))
AC_CHECK_HEADERS([syslog.h],,
    AC_MSG_ERROR([*** syslog.h not found **]))
AC_CHECK_HEADERS([signal.h],,
    AC_MSG_ERROR([*** signal.h not found **]))
AC_CHECK_HEADERS([unistd.h],,
    AC_MSG_ERROR([*** unistd.h not found **]))
AC_CHECK_HEADERS([limits.h],,
    AC_MSG_ERROR([*** limits.h not found **]))
AC_CHECK_HEADERS([time.h],,
    AC_MSG_ERROR([*** time.h not found **]))
AC_CHECK_HEADERS([fcntl.h],,
    AC_MSG_ERROR([*** fcntl.h not found **]))
AC_CHECK_HEADERS([sys/stat.h],,
    AC_MSG_ERROR([*** sys/stat.h not found **]))
AC_CHECK_HEADERS([sys/signal.h],,
    AC_MSG_ERROR([*** sys/stat.h not found **]))
AC_CHECK_HEADERS([uthash.h],,
    AC_MSG_ERROR([*** uthash.h not found **]))
AC_CHECK_HEADERS([pthread.h],,
    AC_MSG_ERROR([*** pthread.h not found **]))
AC_CHECK_HEADERS([curl/curl.h],,
    AC_MSG_ERROR([*** curl.h not found **]))
#AC_CHECK_HEADERS([rest-param.h],,
#    AC_MSG_ERROR([*** rest-param.h not found **]))

# Check for typedefs, structures, and compiler characteristics.
# Check for library functions.
AC_FUNC_MALLOC
AC_CHECK_FUNCS([getopt_long],,
    AC_MSG_ERROR([*** Getopt functions not found. **]))
AC_CHECK_FUNCS([strcasecmp strlen strcat strcpy strncpy isspace],,
    AC_MSG_ERROR([*** String handling functions not found. **]))
AC_CHECK_FUNCS([fopen fprintf fclose getenv system lockf chdir umask dup \
               open write close getdtablesize],,
    AC_MSG_ERROR([*** File handling functions not found. **]))
AC_CHECK_FUNCS([malloc free memset],,
    AC_MSG_ERROR([*** Memory allocation functions not found. **]))
AC_CHECK_FUNCS([openlog syslog closelog],,
    AC_MSG_ERROR([*** Syslog functions not found. **]))
AC_CHECK_FUNCS([time localtime],,
    AC_MSG_ERROR([*** Time functions not found. **]))
AC_CHECK_FUNCS([sigaction fork setsid exit perror pthread_mutex_lock \
	       pthread_mutex_unlock],,
    AC_MSG_ERROR([*** Process functions not found. **]))
# The following functions are not checked:
#
#   config_error_line
#   config_error_text
#
# If the configure script fails, it may be that the following check isn't
# required, since the libopt library has already been determined to be
# available:
AC_CHECK_FUNCS([config_init config_lookup_bool config_lookup_string \
               config_read_file config_destroy],,
AC_MSG_ERROR([*** libopt functions not found. **]))
# If the configure script fails, it may be that the following check isn't
# required, since the FUSE library has already been determined to be
# available:
AC_CHECK_FUNCS([fuse_main],,
    AC_MSG_ERROR([*** FUSE functions not found. **]))

AC_CHECK_FUNCS([curl_easy_init curl_easy_setopt curl_easy_perform \
	       curl_easy_cleanup],,
AC_MSG_ERROR([*** CURL functions not found. **]))
AC_CHECK_FUNCS([rest_xml_parser_new],,
    AC_MSG_ERROR([*** REST functions not found. **]))

# Large file support.
AC_SYS_LARGEFILE

CFLAGS="$CFLAGS -Wall -Werror $DEPS_CFLAGS"
LIBS="$DEPS_LIBS $LIBS"

MAKE_DOXYGEN=false
if @<:@ "x$ac_enable_doc" = "xyes" @:>@; then
    if @<:@ "x$HAVE_DOXYGEN" = "xfalse" -o "x$HAVE_GRAPHVIZ" = "xfalse" @:>@; then
        AC_MSG_ERROR([*** source documentation requested but Doxygen/GraphViz not found])
    else
        MAKE_DOXYGEN=true
    fi
fi
AM_CONDITIONAL(MAKE_DOXYGEN,$MAKE_DOXYGEN)

MAKE_OPENSSL_TESTS=false
if @<:@ "x$ac_enable_openssltest" != "xno" @:>@; then
    if @<:@ $HAVE_OPENSSL = "false" -a "x$ac_enable_openssltest" = "xyes" @:>@; then
        AC_MSG_ERROR([*** OpenSSL tests explicitly enabled, but OpenSSL not found])
    fi
    if @<:@ $HAVE_OPENSSL = "false" -a "x$ac_enable_openssltest" = "xauto" @:>@; then
        AC_MSG_WARN([*** OpenSSL not found, some tests will not be performed])
    fi
    if @<:@ $HAVE_OPENSSL = "true" -a "x$ac_enable_openssltest" != "xno" @:>@; then
        MAKE_OPENSSL_TESTS=true
    fi
fi
AM_CONDITIONAL(MAKE_OPENSSL_TESTS,$MAKE_OPENSSL_TESTS)

# Output files.
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile src/Makefile data/Makefile doc/Makefile doc/man/Makefile test/Makefile])
AC_OUTPUT([doc/main.doc debian/postinst debian/postrm])

echo "
  $PACKAGE_NAME version $PACKAGE_VERSION
  Prefix..........: $prefix
  C Compiler......: $CC $CFLAGS
  C++ Compiler....: $CXX $CXXFLAGS $CPPFLAGS
  Linker..........: $LD $LDFLAGS $LIBS

  Generate source code documentation: $ac_enable_doc
  Perform OpenSSL tests:              $ac_enable_openssltest
"

#  Doxygen.........: ${DOXYGEN:-no}
#  Dot (GraphViz)..: ${GRAPHVIZ:-no}
#  OpenSSL.........: ${OPENSSL:-no}
#  Splint..........: ${SPLINT:-no}
#  Flawfinder......: ${FLAWFINDER:-no}
#  Rats............: ${RATS:-no}
#  Pscan...........: ${PSCAN:-no}
