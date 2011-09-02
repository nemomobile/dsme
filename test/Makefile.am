#
# Generic options
#
ACLOCAL_AMFLAGS = -I m4
AM_CFLAGS = $(C_GENFLAGS) $(C_OPTFLAGS) $(GLIB_CFLAGS) -DDSME_LOG_ENABLE
AM_LDFLAGS = -pthread
AM_CPPFLAGS = $(CPP_GENFLAGS)
LDADD = $(GLIB_LIBS) -ldsme
TESTS = testdriver

#
# Build targets
#
noinst_PROGRAMS = batttest \
		dsmetest \
		dummy_bme \
		processwdtest \
		testdriver \
		abnormalexitwrapper_tester

pkglib_LTLIBRARIES = libabnormalexitwrapper.la

batttest_SOURCES = batttest.c

dummy_bme_SOURCES = dummy_bme.c

dsmetest_SOURCES = dsmetest.c

processwdtest_SOURCES = processwdtest.c

testdriver_SOURCES = testdriver.c

testdriver_CFLAGS = $(C_GENFLAGS) $(C_OPTFLAGS) -DDSME_LOG_ENABLE
testdriver_LDFLAGS = -pthread -rdynamic
testdriver_CPPFLAGS = $(CPP_GENFLAGS) $(GLIB_CFLAGS) $(DBUS_CFLAGS)
# FIXME: including .o files in the following is quite hackish
testdriver_LDADD = $(GLIB_LIBS) -lpthread -ldl -ldsme -lcreds \
                   ../dsme/dsme_server-dsmesock.o \
                   ../dsme/dsme_server-logging.o \
                   ../dsme/dsme_server-mainloop.o \
                   ../dsme/dsme_server-dsme-rd-mode.o

abnormalexitwrapper_tester_SOURCES = abnormalexitwrapper_tester.c
abnormalexitwrapper_tester_LDFLAGS = -pthread -rdynamic

libabnormalexitwrapper_la_SOURCES = abnormalexitwrapper.c
libabnormalexitwrapper_la_CFLAGS = $(C_GENFLAGS) $(C_OPTFLAGS) $(DBUS_CFLAGS) -DDSME_LOG_ENABLE
libabnormalexitwrapper_la_LDFLAGS = -pthread -module -avoid-version -shared -ldl

