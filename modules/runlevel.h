/**
   @file runlevel.h

   DSME internal runlevel changing messages
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

#ifndef DSME_RUNLEVEL_H
#define DSME_RUNLEVEL_H

#include <dsme/state.h>

typedef enum {
  DSME_RUNLEVEL_SHUTDOWN = 0,
  DSME_RUNLEVEL_TEST     = 1,
  DSME_RUNLEVEL_USER     = 2,
  DSME_RUNLEVEL_LOCAL    = 3,
  DSME_RUNLEVEL_ACTDEAD  = 5,
  DSME_RUNLEVEL_REBOOT   = 6,
  DSME_RUNLEVEL_MALF     = 8,
} dsme_runlevel_t;

typedef struct {
  DSMEMSG_PRIVATE_FIELDS
  dsme_runlevel_t runlevel;
} DSM_MSGTYPE_CHANGE_RUNLEVEL;

typedef struct {
  DSMEMSG_PRIVATE_FIELDS
  dsme_runlevel_t runlevel;
} DSM_MSGTYPE_SHUTDOWN;

enum {
  DSME_MSG_ENUM(DSM_MSGTYPE_CHANGE_RUNLEVEL, 0x00000319),
  DSME_MSG_ENUM(DSM_MSGTYPE_SHUTDOWN,        0x00000316),
};

#endif
