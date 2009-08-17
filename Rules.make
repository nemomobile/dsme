.PHONY:	all_dependok clean depend all install local_install
#
# Default values
#

CC	:=	gcc

# C++ suffix
CXX_SUFFIX	:=	cxx
# Dynamic library version
LIBRARY_VERSION	:=	0.0.0

#
# Subdirectory variables
#
BUILD_SUBDIRS	:=	$(addprefix _subbuild_,$(SUBDIRS))
INSTALL_SUBDIRS	:=	$(addprefix _subinstall_,$(SUBDIRS))
CLEAN_SUBDIRS	:=	$(addprefix _subclean_,$(SUBDIRS))
DEPEND_SUBDIRS	:=	$(addprefix _subdepend_,$(SUBDIRS))
DISTCLN_SUBDIRS	+=	$(addprefix _distclean_,$(SUBDIRS))
.PHONY: $(BUILD_SUBDIRS) $(INSTALL_SUBDIRS) $(CLEAN_SUBDIRS) $(DEPEND_SUBDIRS) $(DISTCLN_SUBDIRS)


#
# Complete library names
#
C_A_LIBS	:=	$(addsuffix .a,$(A_LIBRARIES))
C_SO_LIBS	:=	$(addsuffix .so,$(SO_LIBRARIES))
vpath	%.so	$(LD_LIBPATHS)
vpath	%.a	$(LD_LIBPATHS)


#
# C(XX)FLAGS and MKDEP_C(XX)FLAGS (make depend)
#
# Don't try to optimize this, especially do not try single CFLAGS definition
# and conditional assignment OPTFLAGS = DBGFLAGS. It would break overrides.
# CFLAGS may not be immediately evaluated for same reason.
#
ifndef DEBUG
CFLAGS	=	$(C_GENFLAGS) $(C_EXTRA_GENFLAGS) \
		$(C_OPTFLAGS) $(C_EXTRA_OPTFLAGS) \
		$(addprefix -I,$(C_INCDIRS)) \
		$(addprefix -I,$(C_EXTRA_INCDIRS)) \
		$(addprefix -D,$(C_DEFINES)) \
		$(addprefix -D,$(C_EXTRA_DEFINES))
CXXFLAGS  =	$(CXX_GENFLAGS) $(CXX_EXTRA_GENFLAGS) \
		$(CXX_OPTFLAGS) $(CXX_EXTRA_OPTFLAGS) \
		$(addprefix -I,$(CXX_INCDIRS)) \
		$(addprefix -I,$(CXX_EXTRA_INCDIRS)) \
		$(addprefix -D,$(CXX_DEFINES)) \
		$(addprefix -D,$(CXX_EXTRA_DEFINES))
else
CFLAGS	=	$(C_GENFLAGS) $(C_EXTRA_GENFLAGS) \
		$(C_DBGFLAGS) $(C_EXTRA_DBGFLAGS) \
		$(addprefix -I,$(C_INCDIRS)) \
		$(addprefix -I,$(C_EXTRA_INCDIRS)) \
		$(addprefix -D,$(C_DEFINES)) \
		$(addprefix -D,$(C_EXTRA_DEFINES))
CXXFLAGS  =	$(CXX_GENFLAGS) $(CXX_EXTRA_GENFLAGS) \
		$(CXX_DBGFLAGS) $(CXX_EXTRA_DBGFLAGS) \
		$(addprefix -I,$(CXX_INCDIRS)) \
		$(addprefix -I,$(CXX_EXTRA_INCDIRS)) \
		$(addprefix -D,$(CXX_DEFINES)) \
		$(addprefix -D,$(CXX_EXTRA_DEFINES))
endif
MKDEP_CFLAGS	=	$(addprefix -I,$(C_INCDIRS)) \
			$(addprefix -I,$(C_EXTRA_INCDIRS)) \
                        $(MKDEP_INCFLAGS) \
			$(addprefix -D,$(C_DEFINES)) \
			$(addprefix -D,$(C_EXTRA_DEFINES))
MKDEP_CXXFLAGS	=	$(addprefix -I,$(CXX_INCDIRS)) \
			$(addprefix -I,$(CXX_EXTRA_INCDIRS)) \
			$(addprefix -D,$(CXX_DEFINES)) \
			$(addprefix -D,$(CXX_EXTRA_DEFINES))
LDFLAGS	=		$(LD_GENFLAGS) $(LD_EXTRA_GENFLAGS) \
			$(addprefix -L,$(LD_LIBPATHS)) \
			$(addprefix -L,$(LD_EXTRA_LIBPATHS))


#
# All object files
#
C_OBJS    :=	$(sort $(foreach BINARY, $(BINARIES) $(A_LIBRARIES) $(SO_LIBRARIES), $($(BINARY)_C_OBJS)))
CXX_OBJS  :=	$(sort $(foreach BINARY, $(BINARIES) $(A_LIBRARIES) $(SO_LIBRARIES), $($(BINARY)_CXX_OBJS)))
ASM_OBJS  :=	$(sort $(foreach BINARY, $(BINARIES) $(A_LIBRARIES) $(SO_LIBRARIES), $($(BINARY)_ASM_OBJS)))
OBJS	:=	$(C_OBJS) $(CXX_OBJS) $(ASM_OBJS)

#
# Source files
#
C_SRCS    :=	$(C_OBJS:.o=.c)
CXX_SRCS  :=	$(CXX_OBJS:.o=.$(CXX_SUFFIX))
ASM_SRCS  :=	$(ASM_OBJS:.o=.S)
SRCS	:=	$(C_SRCS) $(CXX_SRCS) $(ASM_SRCS)

#
# Helper variable, not to be expanded here. Used to manage target dependencies.
#
TARGET_OBJS	=	$($(1)_C_OBJS) $($(1)_CXX_OBJS) $($(1)_ASM_OBJS)
TARGET_NONSTD_LIBS	=	$(patsubst %,lib%.so,$($(1)_SO_LIBS)) \
				$(patsubst %,lib%.a,$($(1)_A_LIBS))


#
# "make all" targets
#
BUILD_TARGETS	:=	$(BINARIES) \
			$(C_A_LIBS) \
			$(C_SO_LIBS) \
			$(BUILD_SUBDIRS)


#
# Depend is built recursively so possible overrides affect results properly
# Use different .depend file for debug/nondebug
#
ifdef DEBUG
DEPENDNAME	:=	.depend-debug
else
DEPENDNAME	:=	.depend
endif

#
# Targets
#

# all: make sure .depend is up to date, then recursively do real work
all:	$(DEPENDNAME)
	@$(MAKE) --no-print-directory all_dependok

all_dependok: $(BUILD_TARGETS)
	@true


install:  $(DEPENDNAME)
	@$(MAKE) --no-print-directory install_dependok

install_dependok:	$(INSTALL_BINARIES) $(INSTALL_A_LIBRARIES) \
			$(INSTALL_SO_LIBRARIES) $(INSTALL_OTHER)
	@$(MAKE) --no-print-directory install_targetsok INSTALLING=x
ifdef INSTALL_SUBDIRS
	@$(MAKE) $(INSTALL_SUBDIRS)
else
	@true
endif

install_targetsok:	$(INSTALL_BINARIES) $(INSTALL_A_LIBRARIES) \
			$(INSTALL_SO_LIBRARIES) $(INSTALL_INCLUDES) \
			$(INSTALL_OTHER) local_install
	@true


# clean: clean up in subdirectories, then delete targets and intermediates
clean:	$(CLEAN_SUBDIRS)
	rm -f $(BINARIES) $(C_A_LIBS) $(addsuffix *,$(C_SO_LIBS)) $(OBJS)


# distclean: clean and rinse in subdirectiories, then locally.
distclean:	$(DISTCLN_SUBDIRS)
	rm -f $(BINARIES) $(C_A_LIBS) $(addsuffix *,$(C_SO_LIBS)) $(OBJS) $(DISTCLEAN_FILES) .depend .depend-debug
ifdef DISTCLEAN_DIRS
	rm -rf $(DISTCLEAN_DIRS)
endif


# Subdirectory rules
$(BUILD_SUBDIRS):
	@echo Building in directory $(patsubst _subbuild_%, %, $@)...
	@$(MAKE) --no-print-directory -C $(patsubst _subbuild_%, %, $@)
$(INSTALL_SUBDIRS):
	@echo Installing in directory $(patsubst _subinstall_%, %, $@)...
	@$(MAKE) --no-print-directory -C $(patsubst _subinstall_%, %, $@) install
$(CLEAN_SUBDIRS):
	@echo Cleaning in directory $(patsubst _subclean_%, %, $@)...
	@$(MAKE) --no-print-directory -C $(patsubst _subclean_%, %, $@) clean
$(DEPEND_SUBDIRS):
	@echo Depend in directory $(patsubst _subdepend_%, %, $@)...
	@$(MAKE) -o $(DEPENDNAME) --no-print-directory -C $(patsubst _subdepend_%, %, $@) depend
$(DISTCLN_SUBDIRS):
	@echo Distcleaning in directory $(patsubst _distclean_%, %, $@)...
	@$(MAKE) --no-print-directory -C $(patsubst _distclean_%, %, $@) distclean


# Common rule for all executables
ifndef INSTALLING
$(BINARIES):
	$(CC) -o $@ $(LDFLAGS) $(call TARGET_OBJS,$@) $(addprefix -l,$($@_SO_LIBS) $($@_A_LIBS) $($@_LIBS))
endif


# Common rule for all static libraries
ifndef INSTALLING
$(C_A_LIBS):
	$(AR) rf $@ $^
endif


# Common rule for all dynamic libraries
$(C_SO_LIBS):
ifndef INSTALLING
	$(CC) -o $@.$(LIBRARY_VERSION) $(LD_GENFLAGS) -fPIC -shared -Wl,-soname -Wl,$@.$(LIBRARY_VERSION) $(addprefix -l,$($(patsubst %.so,%,$@)_SO_LIBS)) $($(patsubst %.so,%,$@)_EXTRA_LDFLAGS) $^ 
	ln -sf $@.$(LIBRARY_VERSION) $@
else
	@install -d $(INSTALL_DIR)
	install -m $(INSTALL_PERM) -o $(INSTALL_OWNER) -g $(INSTALL_GROUP) $@.$(LIBRARY_VERSION) $(INSTALL_DIR)
	ln -sf $@.$(LIBRARY_VERSION) $(INSTALL_DIR)/$@
endif


# Other (install) rules
ifdef INSTALLING
.PHONY:	$(INSTALL_BINARIES) $(INSTALL_A_LIBRARIES) $(INSTALL_INCLUDES) $(INSTALL_OTHER) $(INSTALL_SO_LIBRARIES)
$(INSTALL_BINARIES) $(INSTALL_A_LIBRARIES) $(INSTALL_INCLUDES) $(INSTALL_OTHER):
	@install -d $(INSTALL_DIR)
	install -m $(INSTALL_PERM) -o $(INSTALL_OWNER) -g $(INSTALL_GROUP) $@ $(INSTALL_DIR)
endif

# Common rule for all C-files. (Note: CFLAGS is evaluated for each target
# separetely.)
.c.o:
	$(CC) -c -o $@ $(CFLAGS) $<


# Common rule for all C++-files. (Note: CXXFLAGS is evaluated for each target
# separetely.)
.$(CXX_SUFFIX).o:
	$(CXX) -c -o $@ $(CXXFLAGS) $<


# .depend and .depend-debug
$(DEPENDNAME) depend:	Makefile $(TOPDIR)/Rules.make
	@rm -f $(DEPENDNAME)
	@touch $(DEPENDNAME)
ifneq "" "$(C_OBJS)$(CXX_OBJS)"
	@$(MAKE) -o $(DEPENDNAME) --no-print-directory LOCAL_DEPEND=$(DEPENDNAME) $(C_OBJS) $(CXX_OBJS) $(DEPEND_SUBDIRS)
endif
ifneq "" "$(BINARIES)"
	@echo -e $(foreach BINARY,$(BINARIES),"\n"$(BINARY): $(call TARGET_OBJS,$(BINARY)) $(call TARGET_NONSTD_LIBS,$(BINARY))) >> $(DEPENDNAME)
endif
ifneq "" "$(A_LIBRARIES)"
	@echo -e $(foreach LIB,$(A_LIBRARIES),"\n"$(LIB).a: $(call TARGET_OBJS,$(LIB))) >> $(DEPENDNAME)
endif
ifneq "" "$(SO_LIBRARIES)"
	@echo -e $(foreach LIB,$(SO_LIBRARIES),"\n"$(LIB).so: $(call TARGET_OBJS,$(LIB))) >> $(DEPENDNAME)
endif


ifdef LOCAL_DEPEND
.PHONY:	$(C_OBJS) $(CXX_OBJS)
$(C_OBJS): %.o: %.c
	@$(CC) >> $(LOCAL_DEPEND) -M $(MKDEP_CFLAGS) $<
$(CXX_OBJS): %.o: %.$(CXX_SUFFIX)
	@$(CXX) >> $(LOCAL_DEPEND) -M $(MKDEP_CXXFLAGS) $<
endif

ifeq ($(DEPENDNAME), $(wildcard $(DEPENDNAME)))
include $(DEPENDNAME)
endif


#
# Variables for sub-makes
#
export CC CXX LD AR MAKE
export CXX_SUFFIX
export C_GENFLAGS C_OPTFLAGS C_DBGFLAGS C_DEFINES C_INCDIRS
export LD_GENFLAGS LD_LIBPATHS
export TOPDIR
export DEBUG
unexport LOCAL_DEPEND INSTALLING
