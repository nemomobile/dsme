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

// to send the init_done signal:
// dbus-send --system --type=signal /com/nokia/startup/signal com.nokia.startup.signal.init_done

// to request a disk space check:
// dbus-send --system --print-reply --dest=com.nokia.diskmonitor /com/nokia/diskmonitor/request com.nokia.diskmonitor.request.req_check

#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif

#define LOGPFIX "diskmonitor: "

#include <iphbd/iphb_internal.h>

#include "dsme_dbus.h"
#include "dbusproxy.h"

#include "diskmonitor.h"
#include "diskmonitor_backend.h"
#include "dsme/modules.h"
#include "dsme/logging.h"
#include "heartbeat.h"

#include <sys/time.h>

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static bool init_done_received           = false;
static bool device_active                = false;

static bool dbus_methods_bound           = false;
static bool dbus_signals_bound           = false;

static time_t last_check_time            = 0;

static const int ACTIVE_CHECK_INTERVAL   = 300;   /* When the device is used, this is how often we check the disk space */
static const int IDLE_CHECK_INTERVAL     = 1800;  /* When the device is not used, this is how often we check the disk space */
static const int MAXTIME_FROM_LAST_CHECK = 900;   /* After this many seconds from the last check, force a check when the device is actived */
static const int CHECK_THRESHOLD         = 60;    /* This is how often we allow disk space check to be requested */

/* ========================================================================= *
 * Helpers
 * ========================================================================= */

static void monotime_get(struct timeval *tv)
{
    struct timespec ts = { 0, 0 };
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1) {
        dsme_log(LOG_WARNING, LOGPFIX"%s: %s", "CLOCK_MONOTONIC",
                 strerror(errno));
    }
    TIMESPEC_TO_TIMEVAL(tv, &ts);
}

static int disk_check_interval(void)
{
    int interval;

    if (device_active) {
        interval = ACTIVE_CHECK_INTERVAL;
    } else {
        interval = IDLE_CHECK_INTERVAL;
    }
    return interval;
}

static void schedule_next_wakeup(void)
{
    DSM_MSGTYPE_WAIT msg = DSME_MSG_INIT(DSM_MSGTYPE_WAIT);
    msg.req.mintime = disk_check_interval();
    msg.req.maxtime = msg.req.mintime + 60;
    msg.req.pid     = 0;
    msg.data        = 0;

    broadcast_internally(&msg);
}

static void check_disk_space(void)
{
    struct timeval monotime;
    monotime_get(&monotime);

    if (init_done_received) {
        check_disk_space_usage(), last_check_time = monotime.tv_sec;
    }
}

/* ========================================================================= *
 * D-Bus Query API
 * ========================================================================= */

static const char diskmonitor_service[]               = "com.nokia.diskmonitor";
static const char diskmonitor_req_interface[]         = "com.nokia.diskmonitor.request";
static const char diskmonitor_sig_interface[]         = "com.nokia.diskmonitor.signal";
static const char diskmonitor_req_path[]              = "/com/nokia/diskmonitor/request";
static const char diskmonitor_sig_path[]              = "/com/nokia/diskmonitor/signal";

static const char diskmonitor_req_check[]             = "req_check";
static const char diskmonitor_disk_space_change_ind[] = "disk_space_change_ind";

static void req_check(const DsmeDbusMessage* request, DsmeDbusMessage** reply)
{
    struct timeval monotime;
    int seconds_from_last_check;

    monotime_get(&monotime);
    seconds_from_last_check = monotime.tv_sec - last_check_time;

    if (seconds_from_last_check >= CHECK_THRESHOLD) {
        check_disk_space();

        schedule_next_wakeup();
    } else {
        dsme_log(LOG_DEBUG,
                 LOGPFIX"%i seconds from the last disk space check request, skip this request",
                 seconds_from_last_check);
    }

    *reply = dsme_dbus_reply_new(request);
}

static const dsme_dbus_binding_t methods[] =
{
    { req_check, diskmonitor_req_check },
    { 0, 0 }
};

static void init_done_ind(const DsmeDbusMessage* ind)
{
    dsme_log(LOG_DEBUG, LOGPFIX"init_done received");

    init_done_received = true;
}

static void mce_inactivity_sig(const DsmeDbusMessage* sig)
{
    const bool inactive                 = dsme_dbus_message_get_bool(sig);
    const bool new_device_active_state  = !inactive;
    struct timeval monotime;
    int seconds_from_last_check;

    monotime_get(&monotime);
    seconds_from_last_check = monotime.tv_sec - last_check_time;

    dsme_log(LOG_DEBUG, LOGPFIX"mce_inactivity_sig received");

    if (new_device_active_state == device_active) {
        /* no change in the inactivity state; don't adjust the schedule */
        return;
    }

    device_active = new_device_active_state;

    if (device_active && seconds_from_last_check >= MAXTIME_FROM_LAST_CHECK) {
        dsme_log(LOG_DEBUG, LOGPFIX"more than %i seconds from the last check, checking",
                 seconds_from_last_check);

        check_disk_space();
    }

    /* adjust the wake-up schedule */
    schedule_next_wakeup();
}

static const dsme_dbus_signal_binding_t signals[] =
{
    { init_done_ind,      "com.nokia.startup.signal", "init_done" },
    { mce_inactivity_sig, "com.nokia.mce.signal",     "system_inactivity_ind" },
    { 0, 0 }
};

/* ========================================================================= *
 * Internal DSME event handling
 * ========================================================================= */

DSME_HANDLER(DSM_MSGTYPE_WAKEUP, client, msg)
{
    check_disk_space();

    schedule_next_wakeup();
}

DSME_HANDLER(DSM_MSGTYPE_DBUS_CONNECT, client, msg)
{
    dsme_log(LOG_DEBUG, LOGPFIX"DBUS_CONNECT");

    dsme_dbus_bind_methods(&dbus_methods_bound, methods, diskmonitor_service, diskmonitor_req_interface);
    dsme_dbus_bind_signals(&dbus_signals_bound, signals);
}

DSME_HANDLER(DSM_MSGTYPE_DBUS_DISCONNECT, client, msg)
{
   dsme_log(LOG_DEBUG, LOGPFIX"DBUS_DISCONNECT");

   dsme_dbus_unbind_methods(&dbus_methods_bound, methods, diskmonitor_service, diskmonitor_req_interface);
   dsme_dbus_unbind_signals(&dbus_signals_bound, signals);
}

DSME_HANDLER(DSM_MSGTYPE_DISK_SPACE, conn, msg)
{
    const char* mount_path = DSMEMSG_EXTRA(msg);
    DsmeDbusMessage* sig =
        dsme_dbus_signal_new(diskmonitor_sig_path,
                             diskmonitor_sig_interface,
                             diskmonitor_disk_space_change_ind);

    dsme_dbus_message_append_string(sig, mount_path);
    dsme_dbus_message_append_int(sig, msg->blocks_percent_used);
    dsme_dbus_signal_emit(sig);
}

module_fn_info_t message_handlers[] =
{
    DSME_HANDLER_BINDING(DSM_MSGTYPE_WAKEUP),
    DSME_HANDLER_BINDING(DSM_MSGTYPE_DBUS_CONNECT),
    DSME_HANDLER_BINDING(DSM_MSGTYPE_DBUS_DISCONNECT),
    DSME_HANDLER_BINDING(DSM_MSGTYPE_DISK_SPACE),
    { 0 }
};

/* ========================================================================= *
 * Plugin init and fini
 * ========================================================================= */

void module_init(module_t* module)
{
    dsme_log(LOG_DEBUG, "diskmonitor.so loaded");

    schedule_next_wakeup();
}

void module_fini(void)
{
    dsme_log(LOG_DEBUG, "diskmonitor.so unloaded");
}
