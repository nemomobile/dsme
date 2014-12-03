/**
   @file usbtracker.c

   Track the USB connection status by listening to usb_moded's indications.
   This is needed for device state selection by the state module,
   so that we will not allow reboot/shutdown while the device is
   mounted to a host PC over USB.
   <p>
   Copyright (C) 2010 Nokia Corporation.
   Copyright (C) 2014 Jolla Ltd

   @author Semi Malinen <semi.malinen@nokia.com>
   @author Simo Piiroinen <simo.piiroinen@jollamobile.com>

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
/*
 * To change usb state, use one of these:
 * dbus-send --type=signal --system /com/meego/dsme com.meego.usb_moded.signal.sig_usb_state_ind string:mass_storage
 * dbus-send --type=signal --system /com/meego/dsme com.meego.usb_moded.signal.sig_usb_state_ind string:none
 */

#include "state-internal.h"

#include "dbusproxy.h"
#include "dsme_dbus.h"

#include "../include/dsme/modules.h"
#include "../include/dsme/logging.h"
#include <dsme/state.h>

#include <string.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

/** Prefix string used for logging from this module */
#define PFIX "usbtracker: "

/* ========================================================================= *
 * Prototypes & variables
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * Broadcast status changes within DSME
 * ------------------------------------------------------------------------- */

static void send_usb_status     (bool mounted_to_pc);
static void send_charger_status (bool charger_connected);

/* ------------------------------------------------------------------------- *
 * Interpreting usb mode strings
 * ------------------------------------------------------------------------- */

/** Lookup table for mode strings usb_moded can be expected to emit
 *
 * The set of available modes is not static. New modes can be added
 * via usb-moded configuration files, but basically
 *
 * - "undefined" means cable is not connected
 * - any other name means cable is connected (=charging should be possible)
 * - some special cases signify that fs is mounted or otherwise directly
 *   accessed via usb (mass storage and mtp modes)
 */
static const struct
{
    const char *mode;
    bool        charging;
    bool        mounted;
} mode_attrs[] =
{
    // mode                        charging mounted
    { "undefined",                  false, false },
    { "mass_storage",               true,  true  },
    { "data_in_use",                true,  true  },
    { "mtp_mode",                   true,  true  },
    { "pc_suite",                   true,  false },
    { "USB connected",              true,  false },
    { "charger_connected",          true,  false },
    { "USB disconnected",           false, false },
    { "charger_disconnected",       false, false },
    { "developer_mode",             true,  false },
    { "charging_only",              true,  false },
    { "dedicated_charger",          true,  false },
    { "mode_requested_show_dialog", true,  false },
    { "ask",                        true,  false },
};

static void evaluate_status (const char *mode, bool *is_charging, bool *is_mounted);
static void update_status   (const char *mode);

/* ------------------------------------------------------------------------- *
 * Waiting for usb_moded to show up on SystemBus
 * ------------------------------------------------------------------------- */

/** Maximum time to wait for usb_moded to show up on SystemBus */
#define WAIT_FOR_USB_MODED_MS (30 * 1000)

/** Timer id: waiting for USB_MODED_DBUS_SERVICE to show up */
static guint wait_for_usb_moded_id = 0;

static gboolean wait_for_usb_moded_cb     (gpointer aptr);
static void     wait_for_usb_moded_cancel (void);
static void     wait_for_usb_moded_start  (void);

/* ------------------------------------------------------------------------- *
 * Availability tracking and D-Bus IPC with usb_moded
 * ------------------------------------------------------------------------- */

/** Well known service name for usb_moded */
#define USB_MODED_DBUS_SERVICE      "com.meego.usb_moded"

/** D-Bus interface name for usb_moded */
#define USB_MODED_DBUS_INTERFACE    "com.meego.usb_moded"

/** D-Bus object name for usb_moded */
#define USB_MODED_DBUS_OBJECT       "/com/meego/usb_moded"

/** Query current usb mode method call */
#define USB_MODED_QUERY_MODE_REQ    "mode_request"

/** Current usb mode changed signal */
#define USB_MODED_MODE_CHANGED_SIG  "sig_usb_state_ind"

static void xusbmoded_mode_changed_cb  (const DsmeDbusMessage *ind);

static void xusbmoded_query_mode_cb    (DBusPendingCall *pending, void *aptr);
static void xusbmoded_query_mode_async (void);

static void xusbmoded_query_owner_cb   (DBusPendingCall *pending, void *aptr);
static bool xusbmoded_query_owner      (void);

static void xusbmoded_set_runstate     (bool running);

static void xusbmoded_init_tracking    (void);
static void xusbmoded_quit_tracking    (void);

static DBusHandlerResult xusbmoded_dbus_filter_cb(DBusConnection *con, DBusMessage *msg, void *aptr);

/* ------------------------------------------------------------------------- *
 * SystemBus connection caching
 * ------------------------------------------------------------------------- */

/** Cached system bus connection */
static DBusConnection *systembus = 0;

static void systembus_connect    (void);
static void systembus_disconnect (void);

/* ========================================================================= *
 * Broadcast status changes within DSME
 * ========================================================================= */

/** Broadcast mounted-over-usb-cable status changes within DSME
 */
static void
send_usb_status(bool mounted_to_pc)
{
    /* Initialize to value that does not match any boolean value */
    static int prev = -1;

    if( prev == mounted_to_pc )
        goto cleanup;

    DSM_MSGTYPE_SET_USB_STATE msg = DSME_MSG_INIT(DSM_MSGTYPE_SET_USB_STATE);

    prev = msg.mounted_to_pc = mounted_to_pc;

    dsme_log(LOG_DEBUG, PFIX"broadcasting usb state:%s mounted to PC",
             msg.mounted_to_pc ? "" : " not");

    broadcast_internally(&msg);

cleanup:

    return;
}

/** Broadcast charger-is-connected status changes within DSME
 */
static void
send_charger_status(bool charger_connected)
{
    /* Initialize to value that does not match any boolean value */
    static int prev = -1;

    if( prev == charger_connected )
        goto cleanup;

    DSM_MSGTYPE_SET_CHARGER_STATE msg = DSME_MSG_INIT(DSM_MSGTYPE_SET_CHARGER_STATE);

    prev = msg.connected = charger_connected;

    dsme_log(LOG_DEBUG, PFIX"broadcasting usb charger state:%s connected",
             msg.connected ? "" : " not");

    broadcast_internally(&msg);

cleanup:

    return;
}

/* ========================================================================= *
 * Interpreting usb mode strings
 * ========================================================================= */

/** Map reported usb mode to charging/mounted-to-pc states
 */
static void
evaluate_status(const char *mode, bool *is_charging, bool *is_mounted)
{
    /* Assume: not-charging, not-mounted */
    bool charging = false;
    bool mounted  = false;

    /* Getting a NULL string here means that for one or another reason
     * we were not able to get the current mode from usb_moded. */
    if( !mode )
        goto cleanup;

    /* Try to lookup from known set of modes */
    for( size_t i = 0; i < G_N_ELEMENTS(mode_attrs); ++i ) {
        if( strcmp(mode_attrs[i].mode, mode) )
            continue;

        charging = mode_attrs[i].charging;
        mounted  = mode_attrs[i].mounted;
        goto cleanup;
    }

    /* The "undefined" that usb_moded uses to signal no usb cable connected
     * is included in the lookup table -> any unknown mode name is assumed
     * to mean that charging should be possible. */

    dsme_log(LOG_WARNING, "unknown usb mode '%s'; assuming charger-connected",
             mode);

    charging = true;
    mounted  = false;

cleanup:

    *is_charging = charging;
    *is_mounted  = mounted;

    return;
}

/** Helper for updating charging/mounted states from reported usb mode
 */
static void
update_status(const char *mode)
{
    dsme_log(LOG_DEBUG, PFIX"mode = '%s'", mode ?: "unknown");

    /* Cancel waiting if we have status update for any reason */
    wait_for_usb_moded_cancel();

    /* Evaluate mode string */
    bool charging = false;
    bool mounted  = false;
    evaluate_status(mode, &charging, &mounted);

    /* Broadcast status changes */
    send_charger_status(charging);
    send_usb_status(mounted);
}

/* ========================================================================= *
 * Waiting for usb_moded to show up on SystemBus
 * ========================================================================= */

/** Timer cb: USB_MODED_DBUS_SERVICE did not show up in expected time
 */
static gboolean
wait_for_usb_moded_cb(gpointer aptr)
{
    (void) aptr; // not used

    if( !wait_for_usb_moded_id )
        goto cleanup;

    wait_for_usb_moded_id = 0;

    /* Since we have no way of knowing what the real status is,
     * assume charger is not connected. If we happen to be in
     * act-dead, this can cause/allow the device to shutdown
     */

    dsme_log(LOG_WARNING, PFIX"usb state unknown; assume: no charger");
    update_status(0);

cleanup:

    return FALSE;
}

/** Stop waiting for USB_MODED_DBUS_SERVICE on SystemBus */
static void
wait_for_usb_moded_cancel(void)
{
    if( wait_for_usb_moded_id ) {
        dsme_log(LOG_DEBUG, PFIX"stop waiting for usb_moded");
        g_source_remove(wait_for_usb_moded_id),
            wait_for_usb_moded_id = 0;
    }
}

/** Start waiting for USB_MODED_DBUS_SERVICE on SystemBus */
static void
wait_for_usb_moded_start(void)
{
    if( !wait_for_usb_moded_id ) {
        dsme_log(LOG_DEBUG, PFIX"start waiting for usb_moded");
        wait_for_usb_moded_id = g_timeout_add(WAIT_FOR_USB_MODED_MS,
                                              wait_for_usb_moded_cb, 0);
    }
}

/* ========================================================================= *
 * Availability tracking and D-Bus IPC with usb_moded
 * ========================================================================= */

/** Handle usb mode change signals
 */
static void
xusbmoded_mode_changed_cb(const DsmeDbusMessage* ind)
{
    const char *dta = dsme_dbus_message_get_string(ind);

    /* Update state; any errors above yield not-connected, not-mounted */
    update_status(dta);
}

/** Handle reply to async query made from xusbmoded_query_mode_async()
 */
static void
xusbmoded_query_mode_cb(DBusPendingCall *pending, void *aptr)
{
    (void) aptr; // not used

    DBusMessage *rsp = 0;
    const char  *dta = 0;
    DBusError    err = DBUS_ERROR_INIT;

    if( !(rsp = dbus_pending_call_steal_reply(pending)) )
        goto cleanup;

    if( dbus_set_error_from_message(&err, rsp) ||
        !dbus_message_get_args(rsp, &err,
                               DBUS_TYPE_STRING, &dta,
                               DBUS_TYPE_INVALID) )
    {
        dsme_log(LOG_ERR, PFIX"mode_request reply: %s: %s",
                 err.name, err.message);
        goto cleanup;
    }

cleanup:

    /* Update state; any errors above yield not-connected, not-mounted */
    update_status(dta);

    if( rsp ) dbus_message_unref(rsp);

    dbus_error_free(&err);
}

/** Initiate async query to find out current usb mode
 */
static void
xusbmoded_query_mode_async(void)
{
    bool             res  = false;
    DBusPendingCall *pc   = 0;
    DBusMessage     *req  = NULL;
    DBusError        err  = DBUS_ERROR_INIT;

    if( !systembus )
        goto cleanup;

    /* we are connected on dbus, now we can query
     * charger/usb connection details */

    req = dbus_message_new_method_call(USB_MODED_DBUS_SERVICE,
                                       USB_MODED_DBUS_OBJECT,
                                       USB_MODED_DBUS_INTERFACE,
                                       USB_MODED_QUERY_MODE_REQ);
    if( !req )
        goto cleanup;

    if( !dbus_connection_send_with_reply(systembus, req, &pc, -1) )
        goto cleanup;

    if( !pc )
        goto cleanup;

    if( !dbus_pending_call_set_notify(pc, xusbmoded_query_mode_cb, 0, 0) )
        goto cleanup;

    res = true;

    dsme_log(LOG_DEBUG, PFIX"mode_request sent");

cleanup:

    if( !res ) {
        dsme_log(LOG_ERR, PFIX"mode_request failed; "
                 "waiting for signal / usb_moded restart");

        /* Wait a while before assuming charger is not connected */
        wait_for_usb_moded_start();
    }

    if( pc )  dbus_pending_call_unref(pc);
    if( req ) dbus_message_unref(req);

    dbus_error_free(&err);
}

/** Change availability of usbmoded on system bus status
 *
 * @param running whether USB_MODED_DBUS_SERVICE has an owner or not
 */
static void
xusbmoded_set_runstate(bool running)
{
    static bool prev = false;

    if( prev == running )
        goto cleanup;

    prev = running;

    dsme_log(LOG_DEBUG, PFIX"usb_moded %s",  running ? "running" : "stopped");

    if( running ) {
        /* Stop wait for service timer and query initial state */
        wait_for_usb_moded_cancel();
        xusbmoded_query_mode_async();
    }
    else {
        /* Wait a while for service to be available again
         * before assuming charger is not connected */
        wait_for_usb_moded_start();
    }

cleanup:

    return;
}

/** Call back for handling asynchronous client verification via GetNameOwner
 *
 * @param pending   control structure for asynchronous d-bus methdod call
 * @param aptr (unused)
 */
static void
xusbmoded_query_owner_cb(DBusPendingCall *pending, void *aptr)
{
    (void) aptr; // not used

    dsme_log(LOG_DEBUG, PFIX"usb_moded runstate reply");

    DBusMessage *rsp = 0;
    const char  *dta = 0;
    DBusError    err = DBUS_ERROR_INIT;

    if( !(rsp = dbus_pending_call_steal_reply(pending)) )
        goto cleanup;

    if( dbus_set_error_from_message(&err, rsp) ||
        !dbus_message_get_args(rsp, &err,
                               DBUS_TYPE_STRING, &dta,
                               DBUS_TYPE_INVALID) ) {
        if( strcmp(err.name, DBUS_ERROR_NAME_HAS_NO_OWNER) ) {
            dsme_log(LOG_WARNING, PFIX"usb_moded name owner reply: %s: %s",
                     err.name, err.message);
        }
        goto cleanup;
    }

cleanup:

    xusbmoded_set_runstate(dta && *dta);

    if( rsp ) dbus_message_unref(rsp);

    dbus_error_free(&err);
}

/** Verify that a usbmoded exists via an asynchronous GetNameOwner method call
 *
 * @return true if the method call was initiated, or false in case of errors
 */
static bool
xusbmoded_query_owner(void)
{
    dsme_log(LOG_DEBUG, PFIX"usb_moded runstate query");

    bool             res  = false;
    DBusMessage     *req  = 0;
    DBusPendingCall *pc   = 0;
    const char      *name = USB_MODED_DBUS_SERVICE;

    if( !systembus )
        goto cleanup;

    req = dbus_message_new_method_call(DBUS_SERVICE_DBUS,
                                       DBUS_PATH_DBUS,
                                       DBUS_INTERFACE_DBUS,
                                       "GetNameOwner");
    if( !req )
        goto cleanup;

    if( !dbus_message_append_args(req,
                                  DBUS_TYPE_STRING, &name,
                                  DBUS_TYPE_INVALID) )
        goto cleanup;

    if( !dbus_connection_send_with_reply(systembus, req, &pc, -1) )
        goto cleanup;

    if( !pc )
        goto cleanup;

    if( !dbus_pending_call_set_notify(pc, xusbmoded_query_owner_cb, 0, 0) )
        goto cleanup;

    res = true;

cleanup:

    if( pc  ) dbus_pending_call_unref(pc);
    if( req ) dbus_message_unref(req);

    return res;
}

/** D-Bus message filter for handling usbmoded NameOwnerChanged signals
 *
 * @param con       dbus connection
 * @param msg       message to be acted upon
 * @param aptr (not used)
 *
 * @return DBUS_HANDLER_RESULT_NOT_YET_HANDLED (other filters see the msg too)
 */
static DBusHandlerResult
xusbmoded_dbus_filter_cb(DBusConnection *con, DBusMessage *msg, void *aptr)
{
    (void) aptr; // not used

    DBusHandlerResult res = DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    const char *sender = 0;
    const char *object = 0;

    const char *name = 0;
    const char *prev = 0;
    const char *curr = 0;

    DBusError err = DBUS_ERROR_INIT;

    if( con != systembus )
        goto cleanup;

    if( !dbus_message_is_signal(msg, DBUS_INTERFACE_DBUS, "NameOwnerChanged") )
        goto cleanup;

    sender = dbus_message_get_sender(msg);
    if( strcmp(sender, DBUS_SERVICE_DBUS) )
        goto cleanup;

    object = dbus_message_get_path(msg);
    if( strcmp(object, DBUS_PATH_DBUS) )
        goto cleanup;

    if( !dbus_message_get_args(msg, &err,
                               DBUS_TYPE_STRING, &name,
                               DBUS_TYPE_STRING, &prev,
                               DBUS_TYPE_STRING, &curr,
                               DBUS_TYPE_INVALID) ) {
        dsme_log(LOG_WARNING, PFIX"usb_moded name owner signal: %s: %s",
                 err.name, err.message);
        goto cleanup;
    }

    if( !strcmp(name, USB_MODED_DBUS_SERVICE) ) {
        dsme_log(LOG_DEBUG, PFIX"usb_moded runstate changed");
        xusbmoded_set_runstate(*curr != 0);
    }

cleanup:

    dbus_error_free(&err);

    return res;
}

/** Signal matching rule for usbmoded name ownership changes */
static const char xusbmoded_name_owner_match[] =
"type='signal'"
",sender='"DBUS_SERVICE_DBUS"'"
",interface='"DBUS_INTERFACE_DBUS"'"
",member='NameOwnerChanged'"
",path='"DBUS_PATH_DBUS"'"
",arg0='"USB_MODED_DBUS_SERVICE"'"
;

/** Start tracking usbmoded state on systembus
 */
static void
xusbmoded_init_tracking(void)
{
    if( !systembus )
        goto cleanup;

    /* Register signal handling filter */
    dbus_connection_add_filter(systembus, xusbmoded_dbus_filter_cb, 0, 0);

    /* NULL error -> match will be added asynchronously */
    dbus_bus_add_match(systembus, xusbmoded_name_owner_match, 0);

    xusbmoded_query_owner();

cleanup:

    return;
}

/** Stop tracking usbmoded state on systembus
 */
static void
xusbmoded_quit_tracking(void)
{
    if( !systembus )
        goto cleanup;

    /* NULL error -> match will be removed asynchronously */
    dbus_bus_remove_match(systembus, xusbmoded_name_owner_match, 0);

    /* Remove signal handling filter */
    dbus_connection_remove_filter(systembus, xusbmoded_dbus_filter_cb, 0);

cleanup:

    return;
}

/* ========================================================================= *
 * SystemBus connection caching
 * ========================================================================= */

/** Get a system bus connection not bound by dsme_dbus abstractions
 *
 * To be called when D-Bus is available notification is received.
 */
static void
systembus_connect(void)
{
    DBusError err = DBUS_ERROR_INIT;

    if( !(systembus = dsme_dbus_get_connection(&err)) ) {
        dsme_log(LOG_WARNING, PFIX"can't connect to systembus: %s: %s",
                 err.name, err.message);
        goto cleanup;
    }

    dbus_connection_setup_with_g_main(systembus, 0);

    xusbmoded_init_tracking();

cleanup:

    dbus_error_free(&err);
}

/** Detach from systembus connection obtained via systembus_connect()
 *
 * To be called at module unload / when D-Bus no longer available
 * notification is received.
 */
static void
systembus_disconnect(void)
{
    if( systembus ) {
        xusbmoded_quit_tracking();
        dbus_connection_unref(systembus), systembus = 0;
    }
}

/* ========================================================================= *
 * Module loading and unloading
 * ========================================================================= */

static const dsme_dbus_signal_binding_t signals[] =
{
    { xusbmoded_mode_changed_cb, USB_MODED_DBUS_INTERFACE, USB_MODED_MODE_CHANGED_SIG },
    { 0, }
};

static bool bound = false;

DSME_HANDLER(DSM_MSGTYPE_DBUS_CONNECT, client, msg)
{
    dsme_log(LOG_DEBUG, PFIX"DBUS_CONNECT");
    dsme_dbus_bind_signals(&bound, signals);
    systembus_connect();

}

DSME_HANDLER(DSM_MSGTYPE_DBUS_DISCONNECT, client, msg)
{
    dsme_log(LOG_DEBUG, PFIX"DBUS_DISCONNECT");
    dsme_dbus_unbind_signals(&bound, signals);
    systembus_disconnect();
}

module_fn_info_t message_handlers[] = {
    DSME_HANDLER_BINDING(DSM_MSGTYPE_DBUS_CONNECT),
    DSME_HANDLER_BINDING(DSM_MSGTYPE_DBUS_DISCONNECT),
    { 0 }
};

void module_init(module_t* handle)
{
    /* Do not connect to D-Bus; it is probably not started yet.
     * Instead, wait for DSM_MSGTYPE_DBUS_CONNECT.
     */

    /* If usb_moded does not show up at SystemBus in reasonale
     * time, assume that charger is not connected */
    wait_for_usb_moded_start();

    dsme_log(LOG_DEBUG, "usbtracker.so loaded");
}

void module_fini(void)
{
    dsme_dbus_unbind_signals(&bound, signals);

    /* Remove timers */
    wait_for_usb_moded_cancel();

    dsme_log(LOG_DEBUG, "usbtracker.so unloaded");
}
