BINARIES := testdriver

C_EXTRA_INCDIRS := ../include
MKDEP_INCFLAGS := $$(pkg-config --cflags-only-I glib-2.0)

testdriver_C_OBJS := testdriver.o
testdriver.o : C_EXTRA_GENFLAGS := $$(pkg-config --cflags glib-2.0)
../modulebase.o : C_EXTRA_GENFLAGS := $$(pkg-config --cflasgs glib-2.0)
testdriver_LIBS := pthread dl
LD_EXTRA_GENFLAGS := -rdynamic \
                     ../dsmesock.o \
                     ../logging.o \
                     ../mainloop.o \
                     -ldsme \
                     $$(pkg-config --libs glib-2.0)

all: $(BINARIES)

.PHONY: run
run:
#	valgrind -q ./testdriver
#	valgrind -q --tool=exp-omega ./testdriver
#	rm -f vcov.out; ~/local/bin/valgrind -q --tool=exp-vcov ./testdriver
#	perl ~/local/bin/vc_annotate vcov.out > vcov.txt
	./testdriver

include $(TOPDIR)/Rules.make
