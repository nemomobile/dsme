#
# Generic options
#
AM_CFLAGS = -Wall -Wmissing-prototypes -std=c99 -Os -g -fPIC
AM_CPPFLAGS = -I$(abs_top_srcdir)/modules -D_GNU_SOURCE
AM_LDFLAGS = -g -Wl,--as-needed

ACLOCAL_AMFLAGS = -I m4

#
# Additional dirs
#
iphbdincludedir = @includedir@/iphbd

#
# Build targets
#
bin_PROGRAMS = hbtest
lib_LTLIBRARIES = libiphb.la

hbtest_SOURCES = hbtest.c
hbtest_LDADD = libiphb.la

libiphb_la_SOURCES = libiphb.c
libiphb_la_LDFLAGS = $(AM_LDFLAGS) -shared

iphbdinclude_HEADERS = libiphb.h