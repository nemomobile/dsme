/**
   @file diskmonitor.h

   <p>
   Copyright (C) 2011 Nokia Corporation.

   @author Matias Muhonen <ext-matias.muhonen@nokia.com>

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

#ifndef DSME_DISKMONITOR_H
#define DSME_DISKMONITOR_H

#include <dsme/messages.h>

typedef struct {
  DSMEMSG_PRIVATE_FIELDS
  /*
   * pointer to the path of the mount point;
   * DANGER: only safe via the internal queue!
   */
  const char*        mount_path;
  /*
   * percent of disk capacity used (0-100)
   */
  unsigned short     blocks_percent_used;
} DSM_MSGTYPE_DISK_SPACE;

enum {
  DSME_MSG_ENUM(DSM_MSGTYPE_DISK_SPACE, 0x00002000),
};


#endif
