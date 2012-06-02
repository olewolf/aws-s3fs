AC_INIT([aws-s3fs], [0.1], [wolf@blazingangles.com])
AC_CONFIG_SRCDIR([src/aws-s3fs.c])
AM_INIT_AUTOMAKE([foreign -Wall -Werror])

# Check for programs.
AC_PROG_CC
AC_PROG_CC_STDC
AM_PROG_CC_C_O
AC_PROG_INSTALL
AC_PROG_MAKE_SET

# Check for modules.
PKG_CHECK_MODULES([libfuse], [fuse >= 2.8])

AC_PROG_TRY_DOXYGEN
# Check for doxygen program.
if test -z "$DOXYGEN"; then
  echo "-----------------------------------------"
  echo " No Doxygen program found - continuing"
  echo " without Doxygen documentation support."
  echo "-----------------------------------------"
fi
AM_CONDITIONAL([HAVE_DOXYGEN],[test -n "$DOXYGEN"])

# Check for libraries.

PKG_CHECK_MODULES(LIBCONFIGXX, libconfig++ >= 1.3,,
    AC_MSG_ERROR([*** libconfig++ source not found - this program requires libconfig++ >= 1.3 **]))
AC_CHECK_LIB([config],[config_init],,
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
AC_CHECK_FUNCS([strcasecmp fopen sprintf sscanf fprintf],,
    AC_MSG_ERROR([*** File or string handling functions not found. **]))

# Output files.
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile src/Makefile])
#AC_OUTPUT([debian/vellemaninstrumentation-modules-_KVERS_.postinst.modules debian/control.modules debian/dkms.conf])
AC_OUTPUT()

echo "
  ($PACKAGE_NAME) version $PACKAGE_VERSION
  Prefix.........: $prefix
  C++ Compiler...: $CXX $CXXFLAGS $CPPFLAGS
  Linker.........: $LD $LDFLAGS $LIBS
  Doxygen........: ${DOXYGEN:-NONE}
"
