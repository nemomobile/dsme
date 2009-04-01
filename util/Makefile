#
# Build targets
#
BINARIES	:=	batttest dsmetool dsmetest bootstate kick_wd waitfordsme kicker thermaltool

#
# Target composition and overrides
#
batttest_C_OBJS		:=	batttest.o
batttest_SO_LIBS	:=	dsme
dsmetool_C_OBJS 	:=	dsmetool.o
dsmetool_SO_LIBS	:=	dsme
dsmetest_C_OBJS 	:=	dsmetest.o
dsmetest_SO_LIBS	:=	dsme
bootstate_C_OBJS 	:=	bootstate.o
bootstate_SO_LIBS	:=	dsme
kick_wd_C_OBJS 	:=	kick_wd.o
kick_wd_SO_LIBS	:=	dsme
waitfordsme_C_OBJS 	:=	waitfordsme.o
waitfordsme_SO_LIBS	:=	dsme
kicker_C_OBJS 	:=	kicker.o
kicker_SO_LIBS	:=	dsme
kicker_LIBS     :=      cal
thermaltool_C_OBJS := thermaltool.o
thermaltool_LIBS   := cal

#
# Install files in this directory
#
INSTALL_PERM    :=      644
INSTALL_OWNER   :=      $(shell id -u)
INSTALL_GROUP   :=      $(shell id -g)


INSTALL_BINARIES                        :=      batttest dsmetool bootstate waitfordsme dsmetest kicker thermaltool dsmetool-session
$(INSTALL_BINARIES): INSTALL_PERM       :=      755
$(INSTALL_BINARIES): INSTALL_DIR        :=      $(DESTDIR)/usr/sbin

include $(TOPDIR)/Rules.make
