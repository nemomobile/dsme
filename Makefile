#
# Build targets
#
BINARIES     := dsme dsme-server
SUBDIRS      := util modules libiphb

VERSION := 0.61.23

#
# Install files in this directory
#
INSTALL_PERM  := 644
INSTALL_OWNER := $(shell id -u)
INSTALL_GROUP := $(shell id -g)

INSTALL_BINARIES                      := dsme dsme-server
$(INSTALL_BINARIES)    : INSTALL_PERM := 755
$(INSTALL_BINARIES)    : INSTALL_DIR  := $(DESTDIR)/sbin

#
# Compiler and tool flags
# C_OPTFLAGS are not used for debug builds (ifdef DEBUG)
# C_DBGFLAGS are not used for normal builds
#
C_GENFLAGS     := -DPRG_VERSION=$(VERSION) -pthread -g -std=c99 \
                  -Wall -Wwrite-strings -Wmissing-prototypes -Werror# -pedantic
C_OPTFLAGS     := -O2 -s
C_DBGFLAGS     := -g -DDEBUG -DDSME_LOG_ENABLE
C_DEFINES      :=
# enable battery thermal mgmt
C_DEFINES      += DSME_BMEIPC
C_INCDIRS      := $(TOPDIR)/include $(TOPDIR)/modules $(TOPDIR)
MKDEP_INCFLAGS := $$(pkg-config --cflags-only-I glib-2.0)


LD_GENFLAGS := -pthread

# If OSSO_DEBUG is defined, compile in the logging
#ifdef OSSO_LOG
C_OPTFLAGS += -DDSME_LOG_ENABLE
#endif

ifneq (,$(findstring DSME_BMEIPC,$(C_DEFINES)))
export DSME_BMEIPC = yes
endif

ifneq (,$(findstring DSME_MEMORY_THERMAL_MGMT,$(C_DEFINES)))
export DSME_MEMORY_THERMAL_MGMT = yes
endif

#
# Target composition and overrides
#

# dsme
dsme_C_OBJS       := dsme-wdd.o dsme-wdd-wd.o oom.o dsme-rd-mode.o
dsme: C_OPTFLAGS  := -O2 -s
dsme: C_GENFLAGS  := -DPRG_VERSION=$(VERSION) -g -std=c99 \
                     -Wall -Wwrite-strings -Wmissing-prototypes -Werror
dsme: C_DEFINES   :=
dsme_LIBS         :=
dsme: LD_GENFLAGS :=


# dsme-server
dsme-server_C_OBJS             := dsme-server.o modulebase.o timers.o \
                                  logging.o oom.o mainloop.o          \
                                  dsmesock.o dsme-rd-mode.o
dsme-server_LIBS               := dsme dl
dsme-server: LD_EXTRA_GENFLAGS := -rdynamic $$(pkg-config --libs gthread-2.0)

#logging.o:	C_EXTRA_DEFINES	:=	USE_STDERR
dsme-server.o : C_EXTRA_GENFLAGS := $$(pkg-config --cflags glib-2.0)
mainloop.o    : C_EXTRA_GENFLAGS := $$(pkg-config --cflags glib-2.0)
modulebase.o  : C_EXTRA_GENFLAGS := $$(pkg-config --cflags glib-2.0)
timers.o      : C_EXTRA_GENFLAGS := $$(pkg-config --cflags glib-2.0)
dsmesock.o    : C_EXTRA_GENFLAGS := $$(pkg-config --cflags glib-2.0)



#
# This is the topdir for build
#
TOPDIR := $(shell /bin/pwd)

#
# Non-target files/directories to be deleted by distclean
#
DISTCLEAN_DIRS	:=	doc tags

#DISTCLN_SUBDIRS := _distclean_tests

#
# Actual rules
#
include $(TOPDIR)/Rules.make

.PHONY: tags
tags:
	find . -name '*.[hc]'  |xargs ctags

.PHONY: doc
doc:
	doxygen

local_install:
	mkdir -p $(DESTDIR)/etc/dsme $(DESTDIR)/etc/dbus-1/system.d
	install -m 600 -o $(INSTALL_OWNER) -g $(INSTALL_GROUP) lifeguard.uids $(DESTDIR)/etc/dsme
	install -m 600 -o $(INSTALL_OWNER) -g $(INSTALL_GROUP) dsme.conf $(DESTDIR)/etc/dbus-1/system.d

install-libiphb install-libiphb-dev install-hbtest:
	@$(MAKE) --no-print-directory -C libiphb $@

.PHONY: test
test: all
	make -C test depend
	make -C test
	make -C test run
