/**
   @file message.c

   Implements DSME message methods.
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

#include "dsme/messages.h"

#include <stdlib.h>

void* dsmemsg_new(u_int32_t id, size_t size, size_t extra)
{
  dsmemsg_generic_t* msg = (dsmemsg_generic_t*)calloc(1, size + extra);
  if (msg == NULL) {
    /* TODO */
    exit(EXIT_FAILURE);
  }

  msg->line_size_ = size + extra;
  msg->size_      = size;
  msg->type_      = id;

  return msg;
}

u_int32_t dsmemsg_id(const dsmemsg_generic_t* msg)
{
  return msg->type_;
}
