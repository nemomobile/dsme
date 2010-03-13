#
# Build targets
#
BINARIES     := dsme dsme-exec-helper
SUBDIRS      := util modules libiphb

VERSION := 0.61.5

#
# Install files in this directory
#
INSTALL_PERM  := 644
INSTALL_OWNER := $(shell id -u)
INSTALL_GROUP := $(shell id -g)

INSTALL_BINARIES                      := dsme dsme-exec-helper
$(INSTALL_BINARIES)    : INSTALL_PERM := 755
$(INSTALL_BINARIES)    : INSTALL_DIR  := $(DESTDIR)/sbin

#
# Compiler and tool flags
# C_OPTFLAGS are not used for debug builds (ifdef DEBUG)
# C_DBGFLAGS are not used for normal builds
#
C_GENFLAGS     := -DPRG_VERSION=$(VERSION) -pthread -g \
                  -Wall -Wwrite-strings -Wmissing-prototypes -Werror# -pedantic
C_OPTFLAGS     := -O2 -s
C_DBGFLAGS     := -g -DDEBUG -DDSME_LOG_ENABLE
C_DEFINES      := DSME_POSIX_TIMER DSME_WD_SYNC
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
dsme_C_OBJS             := dsme.o modulebase.o timers.o \
                           logging.o oom.o mainloop.o \
                           dsme-cal.o dsmesock.o
dsme_LIBS               := dsme dl cal
dsme: LD_EXTRA_GENFLAGS := -rdynamic $$(pkg-config --libs gthread-2.0)

#logging.o:	C_EXTRA_DEFINES	:=	USE_STDERR
dsme.o      : C_EXTRA_GENFLAGS := $$(pkg-config --cflags glib-2.0)
mainloop.o  : C_EXTRA_GENFLAGS := $$(pkg-config --cflags glib-2.0)
modulebase.o: C_EXTRA_GENFLAGS := $$(pkg-config --cflags glib-2.0)
timers.o    : C_EXTRA_GENFLAGS := $$(pkg-config --cflags glib-2.0)
dsmesock.o  : C_EXTRA_GENFLAGS := $$(pkg-config --cflags glib-2.0)


# TODO: move dsme-exec-helper to modules/
# dsme-exec-helper
dsme-exec-helper_C_OBJS := dsme-exec-helper.o oom.o
dsme-exec-helper.o : C_EXTRA_GENFLAGS := $$(pkg-config --cflags glib-2.0)


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
	mkdir -p $(DESTDIR)/etc/dsme
	install -m 600 -o $(INSTALL_OWNER) -g $(INSTALL_GROUP) lifeguard.uids $(DESTDIR)/etc/dsme

install-libiphb install-libiphb-dev install-hbtest:
	@$(MAKE) --no-print-directory -C libiphb $@

.PHONY: test
test: all
	make -C test depend
	make -C test
	make -C test run
