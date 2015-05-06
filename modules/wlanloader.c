/**
   @file wlanloader.c

   @brief Plugin for resetting kernel WLAN module after wifi hotspot usage

   Wlan loader plugin in it's current form simply listens to connman's
   property "Tethering" changes, and resets the wlan kernel module by restarting
   a systemd service called "wlan-module-load.service" each time "Tethering"
   is set to false.

   At start up, the plugin detects if "wlan-module-load.service" is present in
   systemd and only becomes active if it is found. If your hardware platform
   requires WLAN to be reset after wifi hotspot use, please provide a
   systemd system service named "wlan-module-load.service", that at ExecStart
   modprobes the wlan kernel module, and at ExecStop rmmods the module.

   <p>
   Copyright (C) 2014 Jolla Ltd.

   @author Kalle Jokiniemi <kalle.jokiniemi@jolla.com>

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

#include <stdbool.h>
#include <string.h>

#include "dbusproxy.h"
#include "dsme_dbus.h"
#include "../include/dsme/modules.h"
#include "../include/dsme/logging.h"

#define WLAN_SYSTEMD_UNIT   "wlan-module-load.service"

static void reset_wlan_module(void)
{
    DBusError        err  = DBUS_ERROR_INIT;
    DBusConnection  *conn = 0;
    DBusMessage     *req  = NULL;
    const char      *unit = WLAN_SYSTEMD_UNIT;
    const char      *mode = "ignore-requirements";

    dsme_log(LOG_DEBUG, "wlanloader: Resetting WLAN");

    if (!(conn = dsme_dbus_get_connection(&err)))
    {
	dsme_log(LOG_ERR, "wlanloader: system bus connect: %s: %s",
		 err.name, err.message);
        goto cleanup;
    }

    req = dbus_message_new_method_call("org.freedesktop.systemd1",
                               "/org/freedesktop/systemd1",
                               "org.freedesktop.systemd1.Manager",
                               "RestartUnit");
    if (!req)
        goto cleanup;

    if (!dbus_message_append_args(req,
                            DBUS_TYPE_STRING, &unit,
                            DBUS_TYPE_STRING, &mode,
                            DBUS_TYPE_INVALID)) {
        goto cleanup;
    }

    if (!dbus_connection_send(conn, req, NULL))
        goto cleanup;

cleanup:

    if (req) dbus_message_unref(req);
    if (conn) dbus_connection_unref(conn);
    dbus_error_free(&err);
}

static void connman_tethering_changed(const DsmeDbusMessage* sig)
{
    bool tethering;

    if (strcmp(dsme_dbus_message_get_string(sig), "Tethering") == 0 &&
        strcmp(dsme_dbus_message_path(sig),
                            "/net/connman/technology/wifi") == 0)
    {
        tethering = dsme_dbus_message_get_variant_bool(sig);
        dsme_log(LOG_DEBUG, "wlanloader: Tethering status changed to %d",
                                         tethering);

        if (!tethering) {
            reset_wlan_module();
        }
    }
}

static const dsme_dbus_signal_binding_t signals[] = {
  { connman_tethering_changed , "net.connman.Technology", "PropertyChanged" },
  { 0, 0 }
};

static bool bound = false;

static void loader_needed_cb(DBusPendingCall *pending, void *user_data)
{
    (void)user_data;

    DBusMessage *rsp = 0;
    DBusError    err = DBUS_ERROR_INIT;

    if (!(rsp = dbus_pending_call_steal_reply(pending)))
        goto cleanup;

    if (dbus_set_error_from_message(&err, rsp)) {
        dsme_log(LOG_DEBUG, "wlanloader: disabled, GetUnit: %s: %s",
                                                    err.name, err.message);
    } else {
        /* We got the reply without error, so the service exists */
        dsme_dbus_bind_signals(&bound, signals);
        dsme_log(LOG_DEBUG, "wlanloader: activated");
    }

cleanup:
    if (rsp) dbus_message_unref(rsp);
    dbus_error_free(&err);
}

/* Helper function to check if this module is needed */
static void check_loader_needed(void)
{
    DBusError        err  = DBUS_ERROR_INIT;
    DBusPendingCall *pc   = 0;
    DBusConnection  *conn = 0;
    DBusMessage     *req  = NULL;
    const char      *unit = WLAN_SYSTEMD_UNIT;

    if (!(conn = dsme_dbus_get_connection(&err)))
    {
	dsme_log(LOG_ERR, "wlanloader: system bus connect: %s: %s",
		 err.name, err.message);
        goto cleanup;
    }

    req = dbus_message_new_method_call("org.freedesktop.systemd1",
                               "/org/freedesktop/systemd1",
                               "org.freedesktop.systemd1.Manager",
                               "GetUnit");
    if (!req)
        goto cleanup;

    if (!dbus_message_append_args(req,
                            DBUS_TYPE_STRING, &unit,
                            DBUS_TYPE_INVALID)) {
        goto cleanup;
    }

    if (!dbus_connection_send_with_reply(conn, req, &pc, -1))
        goto cleanup;

    if (!pc) {
        dsme_log(LOG_WARNING, "wlanloader: null pending call received");
        goto cleanup;
    }

    if (!dbus_pending_call_set_notify(pc, loader_needed_cb, 0, 0))
        goto cleanup;

cleanup:

    if (pc) dbus_pending_call_unref(pc);
    if (req) dbus_message_unref(req);
    if (conn) dbus_connection_unref(conn);
    dbus_error_free(&err);
}

DSME_HANDLER(DSM_MSGTYPE_DBUS_CONNECT, client, msg)
{
    dsme_log(LOG_DEBUG, "wlanloader: DBUS_CONNECT");

    check_loader_needed();
}

DSME_HANDLER(DSM_MSGTYPE_DBUS_DISCONNECT, client, msg)
{
    dsme_log(LOG_DEBUG, "wlanloader: DBUS_DISCONNECT");
    dsme_dbus_unbind_signals(&bound, signals);
}

module_fn_info_t message_handlers[] = {
    DSME_HANDLER_BINDING(DSM_MSGTYPE_DBUS_CONNECT),
    DSME_HANDLER_BINDING(DSM_MSGTYPE_DBUS_DISCONNECT),
    { 0 }
};

void
module_init(module_t * handle)
{
    dsme_log(LOG_DEBUG, "wlanloader.so loaded");
}

void
module_fini(void)
{
    dsme_dbus_unbind_signals(&bound, signals);
    dsme_log(LOG_DEBUG, "libwlanloader.so unloaded");
}

