/**
   @file shutdownfeedback.c

   Play vibra when shutting down

   <p>
   Copyright (C) 2013 Jolla Oy.

   @author Pekka Lundstrom <pekka.lundstrom@jolla.com>

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

#include "dbusproxy.h"
#include "dsme_dbus.h"

#include "dsme/modules.h"
#include "dsme/logging.h"

#include <dsme/state.h>
#include <dsme/protocol.h>
#include "vibrafeedback.h"

#define PFIX "shutdownfeedback: "
static const char pwroff_event_name[] = "pwroff";

DSME_HANDLER(DSM_MSGTYPE_STATE_CHANGE_IND, conn, msg)
{
    if ((msg->state == DSME_STATE_SHUTDOWN) ||
        (msg->state == DSME_STATE_REBOOT)) {
        //dsme_log(LOG_DEBUG, PFIX"shutdown/reboot state received");
        dsme_play_vibra(pwroff_event_name);
    }
}

DSME_HANDLER(DSM_MSGTYPE_REBOOT_REQ, conn, msg)
{
    // dsme_log(LOG_DEBUG, PFIX"reboot reques received");
    dsme_play_vibra(pwroff_event_name);
}

DSME_HANDLER(DSM_MSGTYPE_SHUTDOWN_REQ, conn, msg)
{
    //dsme_log(LOG_DEBUG, PFIX"shutdown reques received");
    dsme_play_vibra(pwroff_event_name);
}

DSME_HANDLER(DSM_MSGTYPE_DBUS_CONNECT, conn, msg)
{
    dsme_log(LOG_INFO, PFIX"DBUS_CONNECT");
    dsme_ini_vibrafeedback();
}

DSME_HANDLER(DSM_MSGTYPE_DBUS_DISCONNECT, conn, msg)
{
    dsme_log(LOG_INFO, PFIX"DBUS_DISCONNECT");
}

module_fn_info_t message_handlers[] = {
    DSME_HANDLER_BINDING(DSM_MSGTYPE_SHUTDOWN_REQ),
    DSME_HANDLER_BINDING(DSM_MSGTYPE_REBOOT_REQ),
    DSME_HANDLER_BINDING(DSM_MSGTYPE_STATE_CHANGE_IND),
    DSME_HANDLER_BINDING(DSM_MSGTYPE_DBUS_CONNECT),
    DSME_HANDLER_BINDING(DSM_MSGTYPE_DBUS_DISCONNECT),
    {0}
};


void module_init(module_t* handle)
{
    dsme_log(LOG_DEBUG, "shutdownfeedback.so loaded");
}

void module_fini(void)
{
    dsme_fini_vibrafeedback();
    dsme_log(LOG_DEBUG, "shutdownfeedback.so unloaded");
}
