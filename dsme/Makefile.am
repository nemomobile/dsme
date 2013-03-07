#
# Generic options
#
AM_CFLAGS = $(C_GENFLAGS) $(C_OPTFLAGS) -DDSME_LOG_ENABLE
AM_LDFLAGS = -pthread
AM_CPPFLAGS = $(CPP_GENFLAGS)
ACLOCAL_AMFLAGS = -I m4

#
# Additional dirs
#
dbusdir = @sysconfdir@/dbus-1/system.d
pkgsysconfdir = @sysconfdir@/@PACKAGE@

#
# Binaries
#
sbin_PROGRAMS = dsme        \
                dsme-server

#
# dsme
#
dsme_SOURCES = dsme-wdd.c \
               dsme-wdd.h \
               dsme-wdd-wd.c \
               dsme-wdd-wd.h \
               oom.c \
               dsme-rd-mode.c


dsme_CFLAGS = -g -std=c99 -Wall -Wwrite-strings -Wmissing-prototypes -Werror \
              $(C_OPTFLAGS)
dsme_LDFLAGS = -Wl,--as-needed
if WANT_SYSTEMD
dsme_LDADD   = -L/lib -lsystemd-daemon
endif

#
# dsme-server
#
dsme_server_SOURCES = dsme-server.c modulebase.c timers.c logging.c oom.c \
                      mainloop.c dsmesock.c dsme-rd-mode.c
dsme_server_LDFLAGS = $(AM_LDFLAGS) -rdynamic `pkg-config --libs gthread-2.0` -Wl,--as-needed
dsme_server_CPPFLAGS = $(CPP_GENFLAGS) $(GLIB_CFLAGS) -DDSME_LOG_ENABLE
dsme_server_LDADD = $(GLIB_LIBS) -ldsme -ldl

#
# Headers used by all
#
noinst_HEADERS = dsme-rd-mode.h \
                 ../include/dsme/mainloop.h \
                 ../include/dsme/modulebase.h \
                 ../include/dsme/modules.h \
                 ../include/dsme/dsmesock.h \
                 ../include/dsme/logging.h \
                 ../include/dsme/oom.h \
                 ../include/dsme/timers.h


#
# Data
#
dist_dbus_DATA = dsme.conf
dist_pkgsysconf_DATA = lifeguard.uids
