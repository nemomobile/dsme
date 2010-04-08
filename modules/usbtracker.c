/**
   @file usbtracker.c

   Track the USB connection status by listening to usbd's indications.
   This is needed for device state selection by the state module,
   so that we will not allow reboot/shutdown while the device is
   mounted to a host PC over USB.
   <p>
   Copyright (C) 2010 Nokia Corporation.

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
// TODO: update the comment below
/*
 * To change emergency call state, use one of these:
 * dbus-send --type=signal --system /com/nokia/dsme com.nokia.mce.signal.sig_call_state_ind string:none
 * dbus-send --type=signal --system /com/nokia/dsme com.nokia.mce.signal.sig_call_state_ind string:emergency
 */

#include "state-internal.h"

#include "dbusproxy.h"
#include "dsme_dbus.h"

#include "dsme/modules.h"
#include "dsme/logging.h"

#include <string.h>


static void send_usb_status(bool mounted_to_pc)
{
    DSM_MSGTYPE_SET_USB_STATE msg = DSME_MSG_INIT(DSM_MSGTYPE_SET_USB_STATE);

    msg.mounted_to_pc = mounted_to_pc;

    dsme_log(LOG_DEBUG,
             "broadcasting usb state:%s mounted to PC",
             msg.mounted_to_pc ? "" : " not");
    broadcast_internally(&msg);
}

static void usb_state_ind(const DsmeDbusMessage* ind)
{
    static bool mounted_to_pc = false;

    // TODO:
    if (strcmp(dsme_dbus_message_get_string(ind), "mass-storage") == 0) {
        mounted_to_pc = true;
    }

    send_usb_status(mounted_to_pc);
}

// TODO:
static const dsme_dbus_signal_binding_t signals[] = {
    { usb_state_ind, "com.nokia.usb_moded.signal", "state" },
    { 0, 0 }
};

static bool bound = false;

DSME_HANDLER(DSM_MSGTYPE_DBUS_CONNECT, client, msg)
{
  dsme_log(LOG_DEBUG, "DBUS_CONNECT");
  dsme_dbus_bind_signals(&bound, signals);
}

DSME_HANDLER(DSM_MSGTYPE_DBUS_DISCONNECT, client, msg)
{
  dsme_log(LOG_DEBUG, "DBUS_DISCONNECT");
  dsme_dbus_unbind_signals(&bound, signals);
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

  dsme_log(LOG_DEBUG, "libusbracker.so loaded");
}

void module_fini(void)
{
  dsme_dbus_unbind_signals(&bound, signals);

  dsme_log(LOG_DEBUG, "libusbtracker.so unloaded");
}
