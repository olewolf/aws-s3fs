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
#AT_PACKAGE_STRING
#AT_PACKAGE_BUGREPORT
AC_CONFIG_TESTDIR([test])
AC_CONFIG_FILES([test/atlocal])
#AC_CONFIG_FILES([test/m4], [chmod +x test/m4])

# Check for programs.
AC_PROG_CC
AC_PROG_CC_STDC
AM_PROG_CC_C_O
AC_PROG_INSTALL
AC_PROG_MAKE_SET
AC_PATH_PROGS(SPLINT, splint)
AC_PATH_PROGS(FLAWFINDER, flawfinder)
AC_PATH_PROGS(RATS, rats)
AC_PATH_PROGS(PSCAN, pscan)
# Check for doxygen program.
AC_PROG_TRY_DOXYGEN
if test -z "$DOXYGEN"; then
  echo "-----------------------------------------"
  echo " No Doxygen program found - continuing"
  echo " without Doxygen documentation support."
  echo "-----------------------------------------"
fi
AM_CONDITIONAL([HAVE_DOXYGEN],[test -n "$DOXYGEN"])
#AM_CONDITIONAL([HAVE_SPLINT],[test -n "$SPLINT"])

AC_C_CONST
#AC_C_INLINE

# Check for modules.
PKG_CHECK_MODULES([libfuse], [fuse >= 2.8])
PKG_CHECK_MODULES([libconfigxx], libconfig++ >= 1.3,,
    AC_MSG_ERROR([*** libconfig++ source not found - this program requires libconfig++ >= 1.3 **]))

# Check for libraries.
AC_CHECK_LIB([config], [config_init],,
    AC_MSG_ERROR([*** config_init function not found **]))

# Check for header files.
AC_CHECK_HEADERS([stdio.h],,
    AC_MSG_ERROR([*** stdio.h missing **]))
AC_CHECK_HEADERS([string.h],,
    AC_MSG_ERROR([*** string.h missing **]))
AC_CHECK_HEADERS([stdlib.h],,
    AC_MSG_ERROR([*** stdlib.h not found **]))
AC_CHECK_HEADERS([libconfig.h],,
    AC_MSG_ERROR([*** libconfig.h not found **]))
AC_CHECK_HEADERS([getopt.h],,
    AC_MSG_ERROR([*** getopt.h not found **]))

# Check for typedefs, structures, and compiler characteristics.
# Check for library functions.
AC_FUNC_MALLOC
AC_CHECK_FUNCS([getopt_long],,
    AC_MSG_ERROR([*** Getopt functions not found. **]))
AC_CHECK_FUNCS([strcasecmp fopen fprintf],,
    AC_MSG_ERROR([*** File or string handling functions not found. **]))

# Large file support.
AC_SYS_LARGEFILE

CFLAGS="$CFLAGS -Wall -Werror $libconfigxx_CFLAGS $libfuse_CFLAGS"
LIBS="$libfuse_LIBS $libconfigxx_LIBS $LIBS"

# Output files.
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile src/Makefile data/Makefile doc/Makefile doc/man/Makefile test/Makefile])
AC_OUTPUT([src/sysconffile.generate-h])

echo "
  ($PACKAGE_NAME) version $PACKAGE_VERSION
  Prefix.........: $prefix
  C Compiler.....: $CC $CFLAGS
  C++ Compiler...: $CXX $CXXFLAGS $CPPFLAGS
  Linker.........: $LD $LDFLAGS $LIBS
  Doxygen........: ${DOXYGEN:-NONE}
"
#  Splint.........: ${SPLINT:-NO}
