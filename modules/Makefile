#
# Build targets
#
SO_LIBRARIES    := libstartup              \
                   libheartbeat            \
                   libstate                \
                   librunlevel             \
                   libupstart              \
                   libprocesswd            \
                   libdbusproxy            \
                   libalarmtracker         \
                   libthermalmanager       \
                   libemergencycalltracker \
                   libusbtracker           \
                   libiphb                 \
                   librebootloopdetector

ifdef DSME_BMEIPC
SO_LIBRARIES    += libthermalobject_surface
endif

ifdef DSME_MEMORY_THERMAL_MGMT
SO_LIBRARIES    += libthermalobject_memory
endif

SO_LIBRARIES    += libpowerontimer

#
# Target composition and overrides
#
libstartup_C_OBJS            := startup.o
startup.o : C_EXTRA_GENFLAGS := -fPIC

libheartbeat_C_OBJS            := heartbeat.o
heartbeat.o : C_EXTRA_GENFLAGS := -fPIC $$(pkg-config --cflags glib-2.0)

libstate_C_OBJS               := state.o
state.o    : C_EXTRA_GENFLAGS := -fPIC
librunlevel_C_OBJS            := runlevel.o
runlevel.o : C_EXTRA_GENFLAGS := -fPIC
libupstart_C_OBJS             := upstart.o
upstart.o  : C_EXTRA_GENFLAGS := -fPIC $$(pkg-config --cflags dbus-1)
libupstart_EXTRA_LDFLAGS := $$(pkg-config --libs dbus-1)

libprocesswd_C_OBJS := processwd.o
processwd.o : C_EXTRA_GENFLAGS := -fPIC $$(pkg-config --cflags glib-2.0)

libdbusproxy_C_OBJS := dbusproxy.o dsme_dbus.o
dbusproxy.o : C_EXTRA_GENFLAGS := -fPIC $$(pkg-config --cflags glib-2.0)
dsme_dbus.o : C_EXTRA_GENFLAGS := -fPIC $$(pkg-config --cflags dbus-glib-1)
libdbusproxy_EXTRA_LDFLAGS := $$(pkg-config --libs dbus-glib-1) -ldsme_dbus_if

libalarmtracker_C_OBJS := alarmtracker.o
alarmtracker.o : C_EXTRA_GENFLAGS := -fPIC -D_GNU_SOURCE

libthermalmanager_C_OBJS := thermalmanager.o
thermalmanager.o : C_EXTRA_GENFLAGS := -fPIC $$(pkg-config --cflags glib-2.0) -DDSME_THERMAL_TUNING #-DDSME_THERMAL_LOGGING
libthermalmanager_EXTRA_LDFLAGS := -lthermalmanager_dbus_if
#libthermalmanager_EXTRA_LDFLAGS := -lrt

libpowerontimer_C_OBJS := powerontimer.o powerontimer_backend.o
powerontimer.o : C_EXTRA_GENFLAGS := -fPIC $$(pkg-config --cflags glib-2.0) #-DDSME_POWERON_LOGGING
powerontimer_backend.o        : C_EXTRA_GENFLAGS := -fPIC $$(pkg-config --cflags glib-2.0) #-DDSME_POWERON_LOGGING

#libpowerontimer_EXTRA_LDFLAGS := -lpoweronmanager_dbus_if
libpowerontimer_EXTRA_LDFLAGS := -lcal -lrt

ifdef DSME_BMEIPC
libthermalobject_surface_C_OBJS := thermalobject_surface.o thermalsensor_battery.o
thermalobject_surface.o:  C_EXTRA_GENFLAGS := -fPIC
thermalsensor_battery.o : C_EXTRA_GENFLAGS := -fPIC $$(pkg-config --cflags glib-2.0)
libthermalobject_surface_EXTRA_LDFLAGS := -lbmeipc $$(pkg-config --libs glib-2.0)
endif

ifdef DSME_MEMORY_THERMAL_MGMT
libthermalobject_memory_C_OBJS := thermalobject_memory.o thermalsensor_omap.o
thermalobject_memory.o: C_EXTRA_GENFLAGS := -fPIC
thermalsensor_omap.o :  C_EXTRA_GENFLAGS := -fPIC -std=c99
endif

libemergencycalltracker_C_OBJS := emergencycalltracker.o
emergencycalltracker.o : C_EXTRA_GENFLAGS := -fPIC

libusbtracker_C_OBJS := usbtracker.o
usbtracker.o : C_EXTRA_GENFLAGS := -fPIC

libiphb_C_OBJS := iphb.o
iphb.o : C_EXTRA_GENFLAGS := -fPIC $$(pkg-config --cflags glib-2.0) -I ../libiphb
libiphb_EXTRA_LDFLAGS := -lbmeipc $$(pkg-config --libs glib-2.0)

MKDEP_INCFLAGS	:= $$(pkg-config --cflags-only-I glib-2.0 dbus-glib-1) -I ../libiphb

librebootloopdetector_C_OBJS := rebootloopdetector.o
rebootloopdetector.o : C_EXTRA_GENFLAGS := -fPIC

#
# Install files in this directory
#
INSTALL_PERM    :=      644
INSTALL_OWNER   :=      $(shell id -u)
INSTALL_GROUP   :=      $(shell id -g)

$(INSTALL_BINARIES): INSTALL_PERM   :=  755
$(INSTALL_BINARIES): INSTALL_DIR    :=  $(DESTDIR)/sbin
INSTALL_SO_LIBRARIES    :=  libstartup.so        \
                            libheartbeat.so      \
                            libprocesswd.so      \
                            libstate.so          \
                            libupstart.so        \
                            libdbusproxy.so      \
                            libalarmtracker.so   \
                            libthermalmanager.so \
                            libemergencycalltracker.so \
                            libusbtracker.so     \
                            libiphb.so           \
                            librebootloopdetector.so

ifdef DSME_BMEIPC
INSTALL_SO_LIBRARIES	+=	libthermalobject_surface.so
endif
ifdef DSME_MEMORY_THERMAL_MGMT
INSTALL_SO_LIBRARIES	+=	libthermalobject_memory.so
endif

INSTALL_SO_LIBRARIES    +=      libpowerontimer.so

$(INSTALL_SO_LIBRARIES): INSTALL_PERM := 755
$(INSTALL_SO_LIBRARIES): INSTALL_DIR  := $(DESTDIR)/usr/lib/dsme
INSTALL_INCLUDES                      := 
$(INSTALL_INCLUDES): INSTALL_PERM     := 644
$(INSTALL_INCLUDES): INSTALL_DIR      := $(DESTDIR)/usr/include/dsme

include $(TOPDIR)/Rules.make
