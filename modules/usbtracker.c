/**
   @file usbtracker.c

   Track the USB connection status by listening to usb_moded's indications.
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
/*
 * To change usb state, use one of these:
 * dbus-send --type=signal --system /com/meego/dsme com.meego.usb_moded.signal.sig_usb_state_ind string:mass_storage
 * dbus-send --type=signal --system /com/meego/dsme com.meego.usb_moded.signal.sig_usb_state_ind string:none
 */

#include "state-internal.h"

#include "dbusproxy.h"
#include "dsme_dbus.h"

#include "dsme/modules.h"
#include "dsme/logging.h"
#include <dsme/state.h>

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

static void send_charger_status(bool charger_state)
{
    DSM_MSGTYPE_SET_CHARGER_STATE msg = DSME_MSG_INIT(DSM_MSGTYPE_SET_CHARGER_STATE);

    msg.connected = charger_state;

    dsme_log(LOG_DEBUG,
             "broadcasting usb charger state:%s connected",
             msg.connected ? "" : " not");

    broadcast_internally(&msg);
}

static void usb_state_ind(const DsmeDbusMessage* ind)
{
    static bool mounted_to_pc = false, mounted_to_pc_new = false;
    static bool	charger_connected = false, charger_connected_new = false;
    const char* state         = dsme_dbus_message_get_string(ind);

    if (strcmp(state, "mass_storage") == 0 ||
        strcmp(state, "data_in_use" ) == 0)
    {
	mounted_to_pc_new = true;
    }
    if (strcmp(state, "USB connected") == 0 ||
	strcmp(state, "charger_connected") == 0 )
        charger_connected_new = true;
    else if (strcmp(state, "USB disconnected") == 0 ||
             strcmp(state, "charger_disconnected") == 0 )
    {
        charger_connected_new = false;
	mounted_to_pc_new = false;
    }

    if (mounted_to_pc != mounted_to_pc_new)
    {
        mounted_to_pc = mounted_to_pc_new;
        send_usb_status(mounted_to_pc);
    }
    
    if (charger_connected != charger_connected_new)
    {
        charger_connected = charger_connected_new;
        send_charger_status(charger_connected);
    }

}

static const dsme_dbus_signal_binding_t signals[] = {
    { usb_state_ind, "com.meego.usb_moded", "sig_usb_state_ind" },
    { 0, 0 }
};

static bool bound = false;

DSME_HANDLER(DSM_MSGTYPE_DBUS_CONNECT, client, msg)
{
  dsme_log(LOG_DEBUG, "usbtracker: DBUS_CONNECT");
  dsme_dbus_bind_signals(&bound, signals);
  /* we are connected on dbus, now we can query 
     charger/usb connection details */
}

DSME_HANDLER(DSM_MSGTYPE_DBUS_DISCONNECT, client, msg)
{
  dsme_log(LOG_DEBUG, "usbtracker: DBUS_DISCONNECT");
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

  dsme_log(LOG_DEBUG, "usbtracker.so loaded");
}

void module_fini(void)
{
  dsme_dbus_unbind_signals(&bound, signals);

  dsme_log(LOG_DEBUG, "usbtracker.so unloaded");
}
