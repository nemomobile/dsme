#
# Generic options
#
ACLOCAL_AMFLAGS = -I m4
AM_CFLAGS = $(C_GENFLAGS) $(C_OPTFLAGS) $(GLIB_CFLAGS) -DDSME_LOG_ENABLE
AM_LDFLAGS = -pthread
AM_CPPFLAGS = $(CPP_GENFLAGS)
LDADD = $(GLIB_LIBS) -ldsme
TESTS = testmod_state \
	testmod_alarmtracker

#
# Build targets
#
noinst_PROGRAMS = batttest \
		dsmetest \
		dummy_bme \
		processwdtest \
		testmod_state \
		testmod_alarmtracker \
		abnormalexitwrapper_tester

pkglib_LTLIBRARIES = libabnormalexitwrapper.la

batttest_SOURCES = batttest.c

dummy_bme_SOURCES = dummy_bme.c

dsmetest_SOURCES = dsmetest.c

processwdtest_SOURCES = processwdtest.c

testmod_state_SOURCES = testmod_state.c
testmod_state_CFLAGS = $(C_GENFLAGS) $(C_OPTFLAGS) -DDSME_LOG_ENABLE
testmod_state_LDFLAGS = -pthread -rdynamic
testmod_state_CPPFLAGS = $(CPP_GENFLAGS) $(GLIB_CFLAGS) $(DBUS_CFLAGS)
# FIXME: including .o files in the following is quite hackish
testmod_state_LDADD = $(GLIB_LIBS) -ldbus-1 -lpthread -ldl -ldsme -lcreds \
                   ../dsme/dsme_server-dsmesock.o \
                   ../dsme/dsme_server-logging.o \
                   ../dsme/dsme_server-mainloop.o \
                   ../dsme/dsme_server-dsme-rd-mode.o

testmod_alarmtracker_SOURCES = testmod_alarmtracker.c
testmod_alarmtracker_CFLAGS = $(C_GENFLAGS) $(C_OPTFLAGS) -DDSME_LOG_ENABLE
testmod_alarmtracker_LDFLAGS = -pthread -rdynamic
testmod_alarmtracker_CPPFLAGS = $(CPP_GENFLAGS) $(GLIB_CFLAGS) $(DBUS_CFLAGS)
# FIXME: including .o files in the following is quite hackish
testmod_alarmtracker_LDADD = $(GLIB_LIBS) -ldbus-1 -ldbus-glib-1 -lpthread -ldl -ldsme -lcreds \
                   ../dsme/dsme_server-logging.o \
                   ../dsme/dsme_server-mainloop.o
#		   ../modules/dbusproxy_la-dsme_dbus.o
#                   ../dsme/dsme_server-dsmesock.o \
#                   ../dsme/dsme_server-dsme-rd-mode.o


abnormalexitwrapper_tester_SOURCES = abnormalexitwrapper_tester.c
abnormalexitwrapper_tester_LDFLAGS = -pthread -rdynamic

libabnormalexitwrapper_la_SOURCES = abnormalexitwrapper.c
libabnormalexitwrapper_la_CFLAGS = $(C_GENFLAGS) $(C_OPTFLAGS) $(DBUS_CFLAGS) -DDSME_LOG_ENABLE
libabnormalexitwrapper_la_LDFLAGS = -pthread -module -avoid-version -shared -ldl

