/**
   @file heartbeat.h

   Interface to DSME periodic wake up functionality.
   <p>
   Copyright (C) 2009-2010 Nokia Corporation.

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
#ifndef HEARTBEAT_H
#define HEARTBEAT_H

#include "dsme/messages.h"
#include <stdbool.h>

enum {
    DSME_MSG_ENUM(DSM_MSGTYPE_HEARTBEAT_START, 0x00000700),
    DSME_MSG_ENUM(DSM_MSGTYPE_HEARTBEAT_STOP,  0x00000701),
    DSME_MSG_ENUM(DSM_MSGTYPE_HEARTBEAT,       0x00000702),
};

typedef void (dsme_heartbeat_pre_cb_t)(void);
typedef bool (dsme_heartbeat_post_cb_t)(void);

typedef struct {
    DSMEMSG_PRIVATE_FIELDS
    dsme_heartbeat_pre_cb_t*  presleep_cb;
    unsigned                  sleep_interval_in_seconds;
    dsme_heartbeat_post_cb_t* postsleep_cb;
} DSM_MSGTYPE_HEARTBEAT_START;

typedef dsmemsg_generic_t DSM_MSGTYPE_HEARTBEAT_STOP;
typedef dsmemsg_generic_t DSM_MSGTYPE_HEARTBEAT;

#endif
