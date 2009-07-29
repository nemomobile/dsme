/**
   @file hwwd.h

   HW watchdog control messages for DSME
   <p>
   Copyright (C) 2004-2009 Nokia Corporation.

   @author Semi Malinen <semi.malinen@nokia.com>

   This file is part of Dsme.

   Dsme is free software; you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License
   version 2.1 as published by the Free Software Foundation.

   Dsme is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with Dsme.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifndef DSME_HWWD_H
#define DSME_HWWD_H

#include "dsme/messages.h"

#define DSME_HEARTBEAT_INTERVAL 12 /* seconds */

enum {
    DSME_MSG_ENUM(DSM_MSGTYPE_HEARTBEAT,   0x00000700),
    DSME_MSG_ENUM(DSM_MSGTYPE_HWWD_KICK,   0x00000703),
};

typedef dsmemsg_generic_t DSM_MSGTYPE_HEARTBEAT;
typedef dsmemsg_generic_t DSM_MSGTYPE_HWWD_KICK;

#endif
