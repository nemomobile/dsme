/**
   @file dbusproxy.h

   DSME internal D-Bus proxy control messages
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

#ifndef DBUSPROXY_H
#define DBUSPROXY_H

#include <dsme/messages.h>

enum {
    DSME_MSG_ENUM(DSM_MSGTYPE_DBUS_CONNECT,    0x00000100),
    DSME_MSG_ENUM(DSM_MSGTYPE_DBUS_DISCONNECT, 0x00000101),
};

typedef dsmemsg_generic_t DSM_MSGTYPE_DBUS_CONNECT;
typedef dsmemsg_generic_t DSM_MSGTYPE_DBUS_DISCONNECT;

#endif
