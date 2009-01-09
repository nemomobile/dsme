/**
   @file processwd.h

   SW watchdog messages for DSME
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
#ifndef DSME_PROCESSWD_H
#define DSME_PROCESSWD_H

#include "dsme/messages.h"

enum {
    DSME_MSG_ENUM(DSM_MSGTYPE_PROCESSWD_CREATE,       0x000000500),
    DSME_MSG_ENUM(DSM_MSGTYPE_PROCESSWD_DELETE,       0x000000501),
    DSME_MSG_ENUM(DSM_MSGTYPE_PROCESSWD_CLEAR,        0x000000502),
    DSME_MSG_ENUM(DSM_MSGTYPE_PROCESSWD_SET_INTERVAL, 0x000000503),
    DSME_MSG_ENUM(DSM_MSGTYPE_PROCESSWD_PING,         0x000000504),
    DSME_MSG_ENUM(DSM_MSGTYPE_PROCESSWD_PONG,         0x000000504),
    DSME_MSG_ENUM(DSM_MSGTYPE_PROCESSWD_MANUAL_PING,  0x000000505),
};

/**
   Specific message type that is used to request sw watchdog 
   @ingroup message_if
*/
typedef struct {
  DSMEMSG_PRIVATE_FIELDS
  pid_t pid;
} DSM_MSGTYPE_PROCESSWD_PING;

typedef DSM_MSGTYPE_PROCESSWD_PING DSM_MSGTYPE_PROCESSWD_PONG;
typedef DSM_MSGTYPE_PROCESSWD_PING DSM_MSGTYPE_PROCESSWD_CREATE;
typedef DSM_MSGTYPE_PROCESSWD_PING DSM_MSGTYPE_PROCESSWD_DELETE;

typedef dsmemsg_generic_t     DSM_MSGTYPE_PROCESSWD_MANUAL_PING;

typedef struct {
  DSMEMSG_PRIVATE_FIELDS
  u_int32_t timeout;
} DSM_MSGTYPE_PROCESSWD_SET_INTERVAL;

#endif
