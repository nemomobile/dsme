#
# Generic options
#
AM_CFLAGS = $(C_GENFLAGS) $(C_OPTFLAGS) $(GLIB_CFLAGS) -DDSME_LOG_ENABLE
AM_LDFLAGS = -pthread
AM_CPPFLAGS = $(CPP_GENFLAGS)
LDADD = $(GLIB_LIBS) -ldsme

ACLOCAL_AMFLAGS = -I m4

#
# Build targets
#
sbin_PROGRAMS = batttest    \
                dsmetool    \
                dsmetest    \
                bootstate   \
                waitfordsme

sbin_SCRIPTS = dsmetemperature

noinst_PROGRAMS = dsmereboot


batttest_SOURCES = batttest.c

dsmetool_SOURCES = dsmetool.c

dsmetest_SOURCES = dsmetest.c

bootstate_SOURCES = bootstate.c

waitfordsme_SOURCES = waitfordsme.c

dsmereboot_SOURCES = dsmereboot.c
