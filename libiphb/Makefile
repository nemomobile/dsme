# -*- mode: sh -*-

NAME ?= iphbd
ROOT ?= $(DESTDIR)/test-$(NAME)

PREFIX ?= /usr
BINDIR ?= $(PREFIX)/bin
DLLDIR ?= $(PREFIX)/lib
LIBDIR ?= $(PREFIX)/lib
INCDIR ?= $(PREFIX)/include/$(NAME)

SO ?= .so.0

# ----------------------------------------------------------------------------
# Global Flags
# ----------------------------------------------------------------------------

CFLAGS  += -Wall -Wmissing-prototypes
CFLAGS  += -D_GNU_SOURCE
CFLAGS  += -std=c99
CFLAGS  += -Os
#CFLAGS  += -O0
CFLAGS  += -g
CFLAGS  += -I../modules

LDFLAGS += -g

LDLIBS  += -Wl,--as-needed

# flags from pkgtool

PKG_CFLAGS := $(shell pkg-config --cflags $(PKG_NAMES))
PKG_LDLIBS := $(shell pkg-config --libs   $(PKG_NAMES))

# ----------------------------------------------------------------------------
# Top Level Targets
# ----------------------------------------------------------------------------

TARGETS += hbtest
TARGETS += libiphb.a
TARGETS += libiphb$(SO)

.PHONY: build clean distclean mostlyclean install

build:: $(TARGETS)

clean:: mostlyclean
	$(RM) $(TARGETS)

distclean:: clean

mostlyclean::
	$(RM) *.o *~

install:: $(addprefix install-,hbtest libiphb libiphb-dev)

# ----------------------------------------------------------------------------
# Pattern rules
# ----------------------------------------------------------------------------

install-%-bin:
	$(if $<, install -m755 -d $(ROOT)$(BINDIR))
	$(if $<, install -m755 $^ $(ROOT)$(BINDIR))

install-%-lib:
	$(if $<, install -m755 -d $(ROOT)$(LIBDIR))
	$(if $<, install -m755 $^ $(ROOT)$(LIBDIR))

install-%-dll:
	$(if $<, install -m755 -d $(ROOT)$(DLLDIR))
	$(if $<, install -m755 $^ $(ROOT)$(DLLDIR))

install-%-inc:
	$(if $<, install -m755 -d $(ROOT)$(INCDIR))
	$(if $<, install -m755 $^ $(ROOT)$(INCDIR))



% : %.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LDLIBS)


%$(SO): LDFLAGS += -shared -Wl,-soname,$@

%$(SO):
	$(CC) -o $@  $^ $(LDFLAGS) $(LDLIBS)

%.a:
	$(AR) ru $@ $^

%.pic.o : CFLAGS += -fPIC

%.pic.o : %.c
	$(CC) -o $@ -c $< $(CFLAGS)

# ----------------------------------------------------------------------------
# Target dependencies
# ----------------------------------------------------------------------------

hbtest : CFLAGS += $(PKG_CFLAGS)
hbtest : LDLIBS += $(PKG_LDLIBS)
hbtest : hbtest.o libiphb$(SO)

libiphb$(SO) : CFLAGS += $(PKG_CFLAGS)
libiphb$(SO) : LDLIBS += $(PKG_LDLIBS)
libiphb$(SO) : libiphb.pic.o

libiphb.a : CFLAGS += $(PKG_CFLAGS)
libiphb.a : libiphb.o



# ----------------------------------------------------------------------------
# hbtest.deb
# ----------------------------------------------------------------------------

install-hbtest-bin: hbtest

install-hbtest:: $(addprefix install-hbtest-, bin)

# ----------------------------------------------------------------------------
# libiphb.deb
# ----------------------------------------------------------------------------

install-libiphb-dll: libiphb$(SO)

install-libiphb:: $(addprefix install-libiphb-, dll)

# ----------------------------------------------------------------------------
# libiphb-dev.deb
# ----------------------------------------------------------------------------

install-libiphb-dev-inc: libiphb.h
install-libiphb-dev-lib: libiphb.a

install-libiphb-dev:: $(addprefix install-libiphb-dev-, lib inc)
	ln -sf libiphb$(SO) $(ROOT)$(LIBDIR)/libiphb.so

# ----------------------------------------------------------------------------
# Dependency Scanning
# ----------------------------------------------------------------------------

# get -Dmacro && -Idirectory flags from CFLAGS
INC = $(filter -I%,$(CFLAGS) $(PKG_CFLAGS))
MAC = $(filter -D%,$(CFLAGS) $(PKG_CFLAGS))

.PHONY: depend

.depend depend::
	gcc -MM $(MAC) $(INC) *.c | ./depend_filter.py > .depend

ifneq ($(MAKECMDGOALS),depend)
include .depend
endif

# ----------------------------------------------------------------------------
# Prototype Scanning
# ----------------------------------------------------------------------------

.PHONY: %.proto

%.proto : %.c
	cproto $(MAC) $(INC) $< | prettyproto.py

# ----------------------------------------------------------------------------
# CTAG scanning
# ----------------------------------------------------------------------------

.PHONY: tags

tags:
	ctags *.c *.h *.inc

.PHONY: doc
doc:
	doxygen
