/**
   @file malf.h

   DSME internal MALF messages
   <p>
   Copyright (C) 2009-2010 Nokia Corporation.

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

#ifndef DSME_MALF_H
#define DSME_MALF_H

#include <dsme/state.h>

typedef struct {
    DSMEMSG_PRIVATE_FIELDS
} DSM_MSGTYPE_ENTER_MALF;

enum {
  DSME_MSG_ENUM(DSM_MSGTYPE_ENTER_MALF, 0x00000900),
};

#endif
