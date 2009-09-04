#
# Build targets
#
BINARIES	:=	batttest dsmetool dsmetest bootstate kick_wd waitfordsme dsmereboot

#
# Target composition and overrides
#
batttest_C_OBJS		:=	batttest.o
batttest_LIBS	:=	dsme
dsmetool_C_OBJS 	:=	dsmetool.o
dsmetool_LIBS	:=	dsme
dsmetest_C_OBJS 	:=	dsmetest.o
dsmetest_LIBS	:=	dsme
bootstate_C_OBJS 	:=	bootstate.o
bootstate_LIBS	:=	dsme
kick_wd_C_OBJS 	:=	kick_wd.o
kick_wd_LIBS	:=	dsme
waitfordsme_C_OBJS 	:=	waitfordsme.o
waitfordsme_LIBS	:=	dsme
kicker_C_OBJS 	:=	kicker.o
kicker_LIBS	:=	dsme cal
dsmereboot_C_OBJS := dsmereboot.o
dsmereboot_LIBS := dsme


#
# Install files in this directory
#
INSTALL_PERM    :=      644
INSTALL_OWNER   :=      $(shell id -u)
INSTALL_GROUP   :=      $(shell id -g)


INSTALL_BINARIES                        :=      batttest dsmetool bootstate waitfordsme dsmetest dsmetool-session
$(INSTALL_BINARIES): INSTALL_PERM       :=      755
$(INSTALL_BINARIES): INSTALL_DIR        :=      $(DESTDIR)/usr/sbin

include $(TOPDIR)/Rules.make
