/**
   @file lifeguard.h

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
#ifndef DSME_LIFEGUARD_H
#define DSME_LIFEGUARD_H

#include "dsme/messages.h"
#include "spawn.h"
#include <stdbool.h>

enum {
    DSME_MSG_ENUM(DSM_MSGTYPE_PROCESS_START,       0x00000401),
    DSME_MSG_ENUM(DSM_MSGTYPE_PROCESS_STOP,        0x00000402),
    DSME_MSG_ENUM(DSM_MSGTYPE_PROCESS_STARTSTATUS, 0x00000404),
    DSME_MSG_ENUM(DSM_MSGTYPE_LG_NOTICE,           0x00000405),
    DSME_MSG_ENUM(DSM_MSGTYPE_PROCESS_STOPSTATUS,  0x00000406),
};

typedef enum {
    ONCE,
    RESPAWN,
    RESET,
    RESPAWN_FAIL
} process_actions_t;

/**
   Specific message type that is used to start process
   @ingroup message_if
*/
typedef struct {
  DSMEMSG_PRIVATE_FIELDS
  process_actions_t action;
  u_int32_t         restart_limit;
  u_int32_t         restart_period;
  uid_t             uid;
  gid_t             gid;
  int               nice;
  int               oom_adj;
} DSM_MSGTYPE_PROCESS_START;

/**
   Specific message type that is used to stop process
   @ingroup message_if
*/
typedef struct {
  DSMEMSG_PRIVATE_FIELDS
  int  signal;
} DSM_MSGTYPE_PROCESS_STOP;

/**
 * Specific message type that is sent by Lifeguard
 * when a process exits.
 */
enum {
  DSM_LGNOTICE_PROCESS_FAILED,
  DSM_LGNOTICE_PROCESS_RESTART,
  DSM_LGNOTICE_RESET
};

typedef struct {
  DSMEMSG_PRIVATE_FIELDS
  u_int32_t notice_type;
} DSM_MSGTYPE_LG_NOTICE;

typedef DSM_MSGTYPE_PROCESS_EXITED DSM_MSGTYPE_PROCESS_STARTSTATUS;

typedef struct {
  DSMEMSG_PRIVATE_FIELDS
  bool killed;
} DSM_MSGTYPE_PROCESS_STOPSTATUS;

#endif
