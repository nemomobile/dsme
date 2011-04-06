/**
   @file diskmonitor.c
   Periodically monitors the disks and sends a message if the disk space usage
   exceeds the use limits.

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

// to send the base_boot_done signal:
// dbus-send --system --type=signal /com/nokia/startup/signal com.nokia.startup.signal.base_boot_done

#ifndef __cplusplus
#define _GNU_SOURCE
#endif

#include <iphbd/iphb_internal.h>

#include "dsme_dbus.h"
#include "dbusproxy.h"

#include "diskmonitor.h"
#include "diskmonitor_backend.h"
#include "dsme/modules.h"
#include "dsme/logging.h"
#include "heartbeat.h"

#include <stdbool.h>

static bool init_done_received = false;

static void schedule_next_wakeup(void)
{
    /* Don't cause too frequent wakeups for checking the disks;
     * the check is now twice in an hour.
     */

    DSM_MSGTYPE_WAIT msg = DSME_MSG_INIT(DSM_MSGTYPE_WAIT);
    msg.req.mintime = 1800; /* 30 minutes */
    msg.req.maxtime = msg.req.mintime + 120;
    msg.req.pid     = 0;
    msg.data        = 0;

    broadcast_internally(&msg);
}

static bool init_done(void)
{
    return init_done_received;
}

DSME_HANDLER(DSM_MSGTYPE_WAKEUP, client, msg)
{
    if (init_done()) {
        check_disk_space_usage();
    }

    schedule_next_wakeup();
}


static void init_done_ind(const DsmeDbusMessage* ind)
{
    dsme_log(LOG_DEBUG, "base_boot_done");
    init_done_received = true;
}

static bool bound = false;

static const dsme_dbus_signal_binding_t signals[] = {
    { init_done_ind, "com.nokia.startup.signal", "base_boot_done" },
    { 0, 0 }
};

DSME_HANDLER(DSM_MSGTYPE_DBUS_CONNECT, client, msg)
{
  dsme_log(LOG_DEBUG, "diskmonitor: DBUS_CONNECT");
  dsme_dbus_bind_signals(&bound, signals);
}

DSME_HANDLER(DSM_MSGTYPE_DBUS_DISCONNECT, client, msg)
{
  dsme_log(LOG_DEBUG, "diskmonitor: DBUS_DISCONNECT");
  dsme_dbus_unbind_signals(&bound, signals);
}

module_fn_info_t message_handlers[] =
{
  DSME_HANDLER_BINDING(DSM_MSGTYPE_WAKEUP),
  DSME_HANDLER_BINDING(DSM_MSGTYPE_DBUS_CONNECT),
  DSME_HANDLER_BINDING(DSM_MSGTYPE_DBUS_DISCONNECT),
  { 0 }
};

void module_init(module_t* module)
{
    dsme_log(LOG_DEBUG, "diskmonitor.so loaded");

    schedule_next_wakeup();
}

void module_fini(void)
{
    dsme_log(LOG_DEBUG, "diskmonitor.so unloaded");
}
