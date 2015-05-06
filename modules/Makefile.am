#
# Generic options
#
AM_CFLAGS = $(C_GENFLAGS) $(C_OPTFLAGS) -DDSME_LOG_ENABLE -fPIC
AM_LDFLAGS = -pthread -module -avoid-version -shared
AM_CPPFLAGS = $(CPP_GENFLAGS)
ACLOCAL_AMFLAGS = -I m4

pkglib_LTLIBRARIES = startup.la              \
                     heartbeat.la            \
                     state.la                \
                     processwd.la            \
                     dbusproxy.la            \
                     alarmtracker.la         \
                     thermalmanager.la       \
                     emergencycalltracker.la \
                     usbtracker.la           \
                     iphb.la                 \
                     thermalflagger.la       \
                     malf.la                 \
                     diskmonitor.la          \
                     tempreaper.la           \
                     dbusautoconnector.la

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

if WANT_GENERIC_THERMAL_MGMT
pkglib_LTLIBRARIES += thermalsensor_generic.la
endif

if WANT_POWERON_TIMER
pkglib_LTLIBRARIES += powerontimer.la
endif

if WANT_RUNLEVEL
pkglib_LTLIBRARIES += runlevel.la
endif

if WANT_UPSTART
pkglib_LTLIBRARIES += upstart.la
endif

if WANT_VALIDATOR_LISTENER
pkglib_LTLIBRARIES += validatorlistener.la
endif

if WANT_PWRKEY_MONITOR
pkglib_LTLIBRARIES += pwrkeymonitor.la
endif

if WANT_BATTERY_TRACKER
pkglib_LTLIBRARIES += batterytracker.la
endif

if WANT_BOOTREASON_LOGGER
pkglib_LTLIBRARIES += bootreasonlogger.la
endif

if WANT_VIBRA_FEEDBACK
pkglib_LTLIBRARIES += shutdownfeedback.la
endif

if WANT_WLAN_LOADER
pkglib_LTLIBRARIES += wlanloader.la
endif

startup_la_SOURCES = startup.c

# TODO: remove this
# provide a symbolic link libstartup.so -> startup.so to ease transition
install-exec-hook:
	cd $(DESTDIR)$(libdir)/dsme && \
		$(LN_S) startup.so libstartup.so


heartbeat_la_SOURCES = heartbeat.c
heartbeat_la_CFLAGS = $(AM_CFLAGS) $(GLIB_CFLAGS)

state_la_SOURCES = state.c
state_la_CFLAGS = $(AM_CFLAGS) $(DBUS_CFLAGS) -D_GNU_SOURCE
state_la_LIBADD =
if WANT_VIBRA_FEEDBACK
state_la_SOURCES += vibrafeedback.c vibrafeedback.h
state_la_CFLAGS += $(GLIB_CFLAGS) $(LIBNGF_CFLAGS)
state_la_LIBADD += $(GLIB_LIBS) $(LIBNGF_LIBS)
endif

if WANT_RUNLEVEL
runlevel_la_SOURCES = runlevel.c
endif

if WANT_UPSTART
upstart_la_SOURCES = upstart.c
upstart_la_CFLAGS = $(AM_CFLAGS) $(DBUS_CFLAGS)
upstart_la_LIBADD = $(DBUS_LIBS)
endif

if WANT_VALIDATOR_LISTENER
runlevel_la_SOURCES = validatorlistener.c
validatorlistener_la_CFLAGS = $(AM_CFLAGS) $(GLIB_CFLAGS)
endif

processwd_la_SOURCES = processwd.c
processwd_la_CFLAGS = $(AM_CFLAGS) $(GLIB_CFLAGS)

dbusproxy_la_SOURCES = dbusproxy.c dsme_dbus.c
dbusproxy_la_CFLAGS = $(AM_CFLAGS) $(DBUS_CFLAGS) $(DBUSGLIB_CFLAGS)
dbusproxy_la_LIBADD = $(DBUSGLIB_LIBS) -ldsme_dbus_if

alarmtracker_la_SOURCES = alarmtracker.c
alarmtracker_la_CFLAGS = $(AM_CFLAGS) $(DBUS_CFLAGS)
alarmtracker_la_CPPFLAGS = $(AM_CPPFLAGS) -D_GNU_SOURCE

thermalmanager_la_SOURCES = thermalmanager.c thermalobject.c
thermalmanager_la_CFLAGS = $(AM_CFLAGS) $(GLIB_CFLAGS) $(DBUS_CFLAGS)
thermalmanager_la_LIBADD = -lthermalmanager_dbus_if

emergencycalltracker_la_SOURCES = emergencycalltracker.c
emergencycalltracker_la_CFLAGS = $(AM_CFLAGS) $(DBUS_CFLAGS)

usbtracker_la_SOURCES = usbtracker.c
usbtracker_la_CFLAGS = $(AM_CFLAGS) $(GLIB_CFLAGS) $(DBUS_CFLAGS)
usbtracker_la_LIBADD = $(GLIB_LIBS) $(DBUS_LIBS)

iphb_la_SOURCES = iphb.c
iphb_la_CFLAGS = $(AM_CFLAGS) $(GLIB_CFLAGS) $(DBUS_CFLAGS)
iphb_la_CPPFLAGS = $(AM_CPPFLAGS)
iphb_la_LIBADD = $(GLIB_LIBS) $(DBUS_LIBS)

thermalflagger_la_SOURCES = thermalflagger.c

malf_la_SOURCES = malf.c

if WANT_GENERIC_THERMAL_MGMT
thermalsensor_generic_la_SOURCES = thermalsensor_generic.c
thermalsensor_generic_la_CFLAGS = $(AM_CFLAGS) $(GLIB_CFLAGS) $(DBUS_CFLAGS)
thermalsensor_generic_la_LIBADD = -lthermalmanager_dbus_if
endif


if WANT_POWERON_TIMER
powerontimer_la_SOURCES = powerontimer.c         \
                             powerontimer_backend.c \
                             powerontimer.h         \
                             powerontimer_backend.h
powerontimer_la_CFLAGS =  $(AM_CFLAGS) $(GLIB_CFLAGS)
powerontimer_la_LIBADD = -lcal -lrt
endif

diskmonitor_la_SOURCES = diskmonitor.c \
				diskmonitor_backend.c \
				diskmonitor.h \
				diskmonitor_backend.h

diskmonitor_la_CFLAGS = $(AM_CFLAGS) $(DBUS_CFLAGS)

tempreaper_la_SOURCES = tempreaper.c
tempreaper_la_CFLAGS = $(AM_CFLAGS) $(GLIB_CFLAGS)

dbusautoconnector_la_SOURCES = dbusautoconnector.c
dbusautoconnector_la_CFLAGS = $(AM_CFLAGS) $(GLIB_CFLAGS) $(DBUS_CFLAGS)
dbusautoconnector_la_LIBADD = $(GLIB_LIBS)

if WANT_PWRKEY_MONITOR
pwrkeymonitor_la_SOURCES = pwrkeymonitor.c
pwrkeymonitor_la_CFLAGS = $(AM_CFLAGS) $(GLIB_CFLAGS)
pwrkeymonitor_la_LIBADD = $(GLIB_LIBS)
endif

if WANT_BATTERY_TRACKER
batterytracker_la_SOURCES = batterytracker.c
batterytracker_la_CFLAGS = $(AM_CFLAGS) $(GLIB_CFLAGS) $(DBUS_CFLAGS) -D_GNU_SOURCE
batterytracker_la_LIBADD = $(GLIB_LIBS)
endif

if WANT_BOOTREASON_LOGGER 
bootreasonlogger_la_SOURCES = bootreasonlogger.c
bootreasonlogger_la_CFLAGS = $(AM_CFLAGS) $(GLIB_CFLAGS) $(DBUS_CFLAGS) -D_GNU_SOURCE
bootreasonlogger_la_LIBADD = $(GLIB_LIBS)
endif

if WANT_VIBRA_FEEDBACK
shutdownfeedback_la_SOURCES = shutdownfeedback.c vibrafeedback.c vibrafeedback.h
shutdownfeedback_la_CFLAGS = $(AM_CFLAGS) $(GLIB_CFLAGS) $(DBUS_CFLAGS) $(LIBNGF_CFLAGS) -D_GNU_SOURCE
shutdownfeedback_la_LIBADD = $(GLIB_LIBS) $(LIBNGF_LIBS)
endif

if WANT_WLAN_LOADER
wlanloader_la_SOURCES = wlanloader.c
wlanloader_la_CFLAGS = $(AM_CFLAGS) $(DBUS_CFLAGS) -D_GNU_SOURCE
endif
