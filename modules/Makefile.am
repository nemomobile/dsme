#
# Generic options
#
AM_CFLAGS = $(C_GENFLAGS) $(C_OPTFLAGS) -DDSME_LOG_ENABLE -fPIC
AM_LDFLAGS = -pthread
AM_CPPFLAGS = $(CPP_GENFLAGS)
ACLOCAL_AMFLAGS = -I m4

pkglib_LTLIBRARIES = libstartup.la              \
                     libheartbeat.la            \
                     libstate.la                \
                     libprocesswd.la            \
                     libdbusproxy.la            \
                     libalarmtracker.la         \
                     libthermalmanager.la       \
                     libemergencycalltracker.la \
                     libusbtracker.la           \
                     libiphb.la                 \
                     librebootloopdetector.la

noinst_HEADERS = dsme_dbus.h \
                 runlevel.h \
                 heartbeat.h \
                 dbusproxy.h \
                 thermalmanager.h \
                 state-internal.h

#
## Additional dirs
#
pkgsysconfdir = @sysconfdir@


if WANT_BMEIPC
pkglib_LTLIBRARIES += libthermalobject_surface.la
endif

if WANT_MEMORY_THERMAL_MGMT
pkglib_LTLIBRARIES += libthermalobject_memory.la
endif

if WANT_POWERON_TIMER
pkglib_LTLIBRARIES += libpowerontimer.la
endif

if WANT_RUNLEVEL
pkglib_LTLIBRARIES += librunlevel.la
endif

if WANT_UPSTART
pkglib_LTLIBRARIES += libupstart.la
endif

libstartup_la_SOURCES = startup.c

libheartbeat_la_SOURCES = heartbeat.c
libheartbeat_la_CFLAGS = $(AM_CFLAGS) $(GLIB_CFLAGS)

libstate_la_SOURCES = state.c

if WANT_RUNLEVEL
librunlevel_la_SOURCES = runlevel.c
endif

if WANT_UPSTART
libupstart_la_SOURCES = upstart.c
libupstart_la_CFLAGS = $(AM_CFLAGS) $(DBUS_CFLAGS)
libupstart_la_LIBADD = $(DBUS_LIBS)
endif

libprocesswd_la_SOURCES = processwd.c
libprocesswd_la_CFLAGS = $(AM_CFLAGS) $(GLIB_CFLAGS)

libdbusproxy_la_SOURCES = dbusproxy.c dsme_dbus.c
libdbusproxy_la_CFLAGS = $(AM_CFLAGS) $(DBUS_CFLAGS) $(DBUSGLIB_CFLAGS)
libdbusproxy_la_LIBADD = $(DBUSGLIB_LIBS) -ldsme_dbus_if

libalarmtracker_la_SOURCES = alarmtracker.c
libalarmtracker_la_CPPFLAGS = $(AM_CPPFLAGS) -D_GNU_SOURCE

libthermalmanager_la_SOURCES = thermalmanager.c
libthermalmanager_la_CFLAGS = $(AM_CFLAGS) $(GLIB_CFLAGS) -DDSME_THERMAL_TUNING
libthermalmanager_la_LIBADD = -lthermalmanager_dbus_if

libemergencycalltracker_la_SOURCES = emergencycalltracker.c

libusbtracker_la_SOURCES = usbtracker.c

libiphb_la_SOURCES = iphb.c
libiphb_la_CFLAGS = $(AM_CFLAGS) $(GLIB_CFLAGS)
libiphb_la_CPPFLAGS = $(AM_CPPFLAGS)
libiphb_la_LIBADD = -lbmeipc $(GLIB_LIBS)

librebootloopdetector_la_SOURCES = rebootloopdetector.c

if WANT_BMEIPC
libthermalobject_surface_la_SOURCES = thermalobject_surface.c \
                                      thermalsensor_battery.c \
                                      thermalsensor_battery.h
libthermalobject_surface_la_CFLAGS = $(AM_CFLAGS) $(GLIB_CFLAGS)
libthermalobject_surface_la_LIBADD = -lbmeipc $(GLIB_LIBS)
endif

if WANT_MEMORY_THERMAL_MGMT
libthermalobject_memory_la_SOURCES = thermalobject_memory.c \
                                     thermalsensor_omap.c   \
                                     thermalsensor_omap.h
endif


if WANT_POWERON_TIMER
libpowerontimer_la_SOURCES = powerontimer.c         \
                             powerontimer_backend.c \
                             powerontimer.h         \
                             powerontimer_backend.h
libpowerontimer_la_CFLAGS =  $(AM_CFLAGS) $(GLIB_CFLAGS)
libpowerontimer_la_LIBADD = -lcal -lrt
endif
