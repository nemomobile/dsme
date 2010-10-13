# Package name and version
AC_INIT(dsme, 0.6.24)

AM_INIT_AUTOMAKE

AC_PREREQ([2.65])
AC_CONFIG_SRCDIR([dsme-wdd-wd.c])
#AC_CONFIG_HEADERS([config.h])

#
# CONFIG OPTIONS
#

# bmeipc
AC_ARG_WITH([bmeipc],
  [AS_HELP_STRING([--without-bmeipc],
    [disable BME ipc communication])])

AS_IF([test "x$with_bmeipc" != xno],
  [AC_CHECK_LIB([bmeipc], [bmeipc_open],
    [AC_SUBST([LIBBMEIPC], ["-lbmeipc"])
     AC_DEFINE([DSME_BMEIPC], [1])
    ],
    [AC_MSG_FAILURE([libbmeipc not found (--without-bmeipc to disable)])])
  ])

AM_CONDITIONAL([WANT_BMEIPC], [test x$with_bmeipc != xno])

#
# powerontimer
#
AC_ARG_ENABLE([poweron-timer],
  [AS_HELP_STRING([--disable-poweron-timer],
    [disable power-on timer plugin (libpowerontimer)])])

AS_IF([test "x$enable_poweron_timer" != xno],
  [AC_DEFINE([DSME_POWERON_TIMER], [1], [Enable power-on timer plugin])
   AC_CHECK_LIB([cal], [cal_init], [],
     [AC_MSG_FAILURE([libcal not found (needed by poweron-timer plugin)])])
  ])

AM_CONDITIONAL([WANT_POWERON_TIMER], [test x$enable_poweron_timer != xno])

#
# runlevel
#
AC_ARG_ENABLE([runlevel],
  [AS_HELP_STRING([--enable-runlevel],
    [enable runlevel plugin (librunlevel) - for systems with SysV init])],
  [],
  [enable_runlevel=no])

AS_IF([test "x$enable_runlevel" != xno],
  [AC_DEFINE([DSME_WANT_LIBRUNLEVEL], [1], [Enable runlevel plugin])])
AM_CONDITIONAL([WANT_RUNLEVEL], [test x$enable_runlevel != xno])

#
# upstart
#
AC_ARG_ENABLE([upstart],
  [AS_HELP_STRING([--disable-upstartl],
    [disable upstart plugin (libupstart)])])

AS_IF([test "x$enable_upstart" != xno],
  [AC_DEFINE([DSME_WANT_LIBUPSTART], [1])])
AM_CONDITIONAL([WANT_UPSTART], [test x$enable_upstart != xno])

#
# memory thermal mgmt
#
AC_ARG_ENABLE([memory-thermal-mgmt],
  [AS_HELP_STRING([--enable-memory-thermal-mgmt],
    [enable memory thermal management (libthermalobject_memory)])],
  [],
  [enable_memory_thermal_mgmt=no])

AS_IF([test "x$enable_memory_thermal_mgmt" != xno],
  [AC_DEFINE([DSME_MEMORY_THERMAL_MGMT], [1])])
AM_CONDITIONAL([WANT_MEMORY_THERMAL_MGMT], [test x$enable_memory_thermal_mgmt != xno])


#
# Compiler and linker flags
#
AC_DEFINE_UNQUOTED([PRG_VERSION],[$VERSION],[Version for DSME])
# FIXME: DSME_LOG_ENABLE should be proper&working configure option
#AC_DEFINE([DSME_LOG_ENABLE], [1])

AC_SUBST(C_GENFLAGS, ["-pthread -g -std=c99 -Wall -Wwrite-strings -Wmissing-prototypes -Werror"])
AC_SUBST(C_OPTFLAGS, ["-O2 -s"])
AC_SUBST(C_DBGFLAGS, ["-g -DDEBUG -DDSME_LOG_ENABLE"])
AC_SUBST(CPP_GENFLAGS, '-I$(abs_top_srcdir) -I$(abs_top_srcdir)/include')


# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_PROG_LIBTOOL
PKG_PROG_PKG_CONFIG

# Specify dir for local macros
AC_CONFIG_MACRO_DIR([m4])

# Enable per-target compiler options
AM_PROG_CC_C_O()

# Pkgconf checks
PKG_CHECK_MODULES(GLIB, glib-2.0)
PKG_CHECK_MODULES(DBUS, dbus-1)
PKG_CHECK_MODULES(DBUSGLIB, dbus-glib-1)

# Check libs (that are not yet checked)
# Whitespaces in 'action-if-found' fields in order to not (auto)update LIBS variable
AC_CHECK_LIB([rt], [clock_nanosleep], [ ], AC_MSG_FAILURE([librt required!]))
AC_CHECK_LIB([dsme], [dsmesock_init], [ ], AC_MSG_FAILURE([libdsme required!]), [$GLIB_LIBS])

# Check headers
AC_CHECK_HEADERS([arpa/inet.h fcntl.h inttypes.h limits.h stdint.h stdlib.h \
                  string.h strings.h sys/ioctl.h sys/socket.h sys/time.h    \
                  syslog.h unistd.h utmpx.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_TYPE_UID_T
AC_C_INLINE
AC_TYPE_INT16_T
AC_TYPE_INT32_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT8_T

# Check common library functions
AC_FUNC_ALLOCA
AC_FUNC_FORK
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_FUNC_STRTOD
AC_CHECK_FUNCS([atexit clock_gettime dup2 gettimeofday inet_ntoa memmove        \
                memset realpath select setenv socket strcasecmp strdup strerror \
                strndup strpbrk strrchr strstr strtoul uname utmpxname])

# Create files
AC_CONFIG_FILES([Makefile
                 modules/Makefile
                 libiphb/Makefile
                 util/Makefile
                 test/Makefile])

AC_OUTPUT
