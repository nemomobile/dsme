#
# Build targets
#
SO_LIBRARIES    := libstartup              \
                   libstate                \
                   librunlevel             \
                   liblifeguard            \
                   libprocesswd            \
                   libhwwd                 \
                   libdbusproxy            \
                   libalarmtracker         \
                   libthermalmanager       \
                   libemergencycalltracker

ifdef DSME_BMEIPC
SO_LIBRARIES    += libthermalobject_surface
endif

ifdef DSME_MEMORY_THERMAL_MGMT
SO_LIBRARIES    += libthermalobject_memory
endif


#
# Target composition and overrides
#
libstartup_C_OBJS            := startup.o
startup.o : C_EXTRA_GENFLAGS := -fPIC

libstate_C_OBJS               := state.o
state.o    : C_EXTRA_GENFLAGS := -fPIC
librunlevel_C_OBJS            := runlevel.o
runlevel.o : C_EXTRA_GENFLAGS := -fPIC

liblifeguard_C_OBJS := lifeguard.o spawn.o
lifeguard.o : C_EXTRA_GENFLAGS := -fPIC $$(pkg-config --cflags glib-2.0)
spawn.o     : C_EXTRA_GENFLAGS := -fPIC $$(pkg-config --cflags glib-2.0)

libprocesswd_C_OBJS := processwd.o
processwd.o : C_EXTRA_GENFLAGS := -fPIC $$(pkg-config --cflags glib-2.0)

libhwwd_C_OBJS               := hwwd.o dsme_wd.o
hwwd.o    : C_EXTRA_GENFLAGS := -fPIC
dsme_wd.o : C_EXTRA_GENFLAGS := -fPIC
libhwwd_EXTRA_LDFLAGS        := -lpthread

libdbusproxy_C_OBJS := dbusproxy.o dsme_dbus.o
dbusproxy.o : C_EXTRA_GENFLAGS := -fPIC $$(pkg-config --cflags glib-2.0)
dsme_dbus.o : C_EXTRA_GENFLAGS := -fPIC $$(pkg-config --cflags dbus-glib-1)
libdbusproxy_EXTRA_LDFLAGS := $$(pkg-config --libs dbus-glib-1) ../libdsme_dbus_if.so

libalarmtracker_C_OBJS := alarmtracker.o
alarmtracker.o : C_EXTRA_GENFLAGS := -fPIC

libthermalmanager_C_OBJS := thermalmanager.o
thermalmanager.o : C_EXTRA_GENFLAGS := -fPIC $$(pkg-config --cflags glib-2.0) -DDSME_THERMAL_TUNING #-DDSME_THERMAL_LOGGING
#libthermalmanager_EXTRA_LDFLAGS := -lrt

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

MKDEP_INCFLAGS	:= $$(pkg-config --cflags-only-I glib-2.0 dbus-glib-1)

#
# Install files in this directory
#
INSTALL_PERM    :=      644
INSTALL_OWNER   :=      $(shell id -u)
INSTALL_GROUP   :=      $(shell id -g)

$(INSTALL_BINARIES): INSTALL_PERM   :=  755
$(INSTALL_BINARIES): INSTALL_DIR    :=  $(DESTDIR)/sbin
INSTALL_SO_LIBRARIES    :=  libstartup.so libprocesswd.so \
                            libhwwd.so liblifeguard.so \
                            libstate.so \
                            librunlevel.so \
                            libdbusproxy.so \
                            libalarmtracker.so \
                            libthermalmanager.so \
                            libemergencycalltracker.so
ifdef DSME_BMEIPC
INSTALL_SO_LIBRARIES	+=	libthermalobject_surface.so
endif
ifdef DSME_MEMORY_THERMAL_MGMT
INSTALL_SO_LIBRARIES	+=	libthermalobject_memory.so
endif
$(INSTALL_SO_LIBRARIES): INSTALL_PERM := 755
$(INSTALL_SO_LIBRARIES): INSTALL_DIR  := $(DESTDIR)/usr/lib/dsme
INSTALL_INCLUDES                      := dsme_dbus_if.h \
                                         state.h        \
                                         state_states.h \
                                         processwd.h
$(INSTALL_INCLUDES): INSTALL_PERM     := 644
$(INSTALL_INCLUDES): INSTALL_DIR      := $(DESTDIR)/usr/include/dsme

include $(TOPDIR)/Rules.make
