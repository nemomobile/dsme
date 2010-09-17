#
# Generic options
#
ACLOCAL_AMFLAGS = -I m4

#
# Build targets
#
sbin_PROGRAMS = testdriver

testdriver_CFLAGS = $(C_GENFLAGS) $(C_OPTFLAGS) -DDSME_LOG_ENABLE
testdriver_LDFLAGS = -pthread -rdynamic
testdriver_CPPFLAGS = $(CPP_GENFLAGS) $(GLIB_CFLAGS)
# FIXME: including .o files in the following is quite hackish
testdriver_LDADD = $(GLIB_LIBS) -lpthread -ldl -ldsme \
                   ../dsme_server-dsmesock.o          \
                   ../dsme_server-logging.o           \
                   ../dsme_server-mainloop.o