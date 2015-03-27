/**
   @file dbusproxy.c

   This module implements proxying of between DSME's internal message
   queue and D-Bus.
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

/*
 * An example command line to obtain dsme version number over D-Bus:
 * $ dbus-send --system --print-reply --dest=com.nokia.dsme /com/nokia/dsme com.nokia.dsme.request.get_version
 *
 * TODO:
 * - dsme should cope with D-Bus restarts
 */
#include "dbusproxy.h"
#include "dsme_dbus.h"


#include "../include/dsme/modules.h"
#include "../include/dsme/logging.h"

#include <dsme/state.h>
#include <dsme/dsme_dbus_if.h>

#include <glib.h>
#include <stdlib.h>


static const char* const service       = dsme_service;
static const char* const req_interface = dsme_req_interface;
static const char* const sig_interface = dsme_sig_interface;
static const char* const sig_path      = dsme_sig_path;


static char* dsme_version = 0;

static void get_version(const DsmeDbusMessage* request, DsmeDbusMessage** reply)
{
  *reply = dsme_dbus_reply_new(request);
  dsme_dbus_message_append_string(*reply,
                                  dsme_version ? dsme_version : "unknown");
}

// list of unanswered state query replies from D-Bus
static GSList* state_replies = 0;

static void get_state(const DsmeDbusMessage* request, DsmeDbusMessage** reply)
{
    // first create a reply and append it to the list of yet unsent replies
    state_replies = g_slist_append(state_replies, dsme_dbus_reply_new(request));

    // then proxy the query to the internal message queue
    DSM_MSGTYPE_STATE_QUERY query = DSME_MSG_INIT(DSM_MSGTYPE_STATE_QUERY);
    broadcast_internally(&query);
}

static void req_powerup(const DsmeDbusMessage* request, DsmeDbusMessage** reply)
{
  char* sender = dsme_dbus_endpoint_name(request);
  dsme_log(LOG_NOTICE,
           "powerup request received over D-Bus from %s",
           sender ? sender : "(unknown)");
  free(sender);

  DSM_MSGTYPE_POWERUP_REQ req = DSME_MSG_INIT(DSM_MSGTYPE_POWERUP_REQ);
  broadcast_internally(&req);
}

static void req_reboot(const DsmeDbusMessage* request, DsmeDbusMessage** reply)
{
  char* sender = dsme_dbus_endpoint_name(request);
  dsme_log(LOG_NOTICE,
           "reboot request received over D-Bus from %s",
           sender ? sender : "(unknown)");
  free(sender);

  DSM_MSGTYPE_REBOOT_REQ req = DSME_MSG_INIT(DSM_MSGTYPE_REBOOT_REQ);
  broadcast_internally(&req);
}

static void req_shutdown(const DsmeDbusMessage* request,
                         DsmeDbusMessage**      reply)
{
  char* sender = dsme_dbus_endpoint_name(request);
  dsme_log(LOG_NOTICE,
           "shutdown request received over D-Bus from %s",
           sender ? sender : "(unknown)");
  free(sender);

  DSM_MSGTYPE_SHUTDOWN_REQ req = DSME_MSG_INIT(DSM_MSGTYPE_SHUTDOWN_REQ);

  broadcast_internally(&req);
}

static const dsme_dbus_binding_t methods[] = {
  { get_version,  dsme_get_version  },
  { get_state,    dsme_get_state    },
  { req_powerup,  dsme_req_powerup  },
  { req_reboot,   dsme_req_reboot   },
  { req_shutdown, dsme_req_shutdown },
  { 0, 0 }
};


static bool bound = false;


static const char* shutdown_action_name(dsme_state_t state)
{
    return (state == DSME_STATE_REBOOT ? "reboot" : "shutdown");
}

static const struct {
    int         value;
    const char* name;
} states[] = {
#define DSME_STATE(STATE, VALUE) { VALUE, #STATE },
#include <dsme/state_states.h>
#undef  DSME_STATE
};

static const char* state_name(dsme_state_t state)
{
    int         index;
    const char* name = "UNKNOWN";;

    for (index = 0; index < sizeof states / sizeof states[0]; ++index) {
        if (states[index].value == state) {
            name = states[index].name;
            break;
        }
    }

    return name;
}

static void emit_dsme_dbus_signal(const char* name)
{
  DsmeDbusMessage* sig = dsme_dbus_signal_new(sig_path, sig_interface, name);
  dsme_dbus_signal_emit(sig);
}

DSME_HANDLER(DSM_MSGTYPE_STATE_CHANGE_IND, server, msg)
{
    if (state_replies) {
        // there are yet unsent replies to state queries; sent the first one
        GSList* first_node = state_replies;
        DsmeDbusMessage* first_reply = (DsmeDbusMessage*)(first_node->data);
        dsme_dbus_message_append_string(first_reply, state_name(msg->state));
        dsme_dbus_signal_emit(first_reply); // deletes the reply
        first_node->data = 0;
        state_replies = g_slist_delete_link(state_replies, first_node);
    } else {
        // this is a broadcast state change
        if (msg->state == DSME_STATE_SHUTDOWN ||
            msg->state == DSME_STATE_ACTDEAD  ||
            msg->state == DSME_STATE_REBOOT)
        {
            emit_dsme_dbus_signal(dsme_shutdown_ind);
        }

        DsmeDbusMessage* sig = dsme_dbus_signal_new(sig_path,
                                                    sig_interface,
                                                    dsme_state_change_ind);
        dsme_dbus_message_append_string(sig, state_name(msg->state));
        dsme_dbus_signal_emit(sig);
    }
}

DSME_HANDLER(DSM_MSGTYPE_BATTERY_EMPTY_IND, server, msg)
{
  emit_dsme_dbus_signal(dsme_battery_empty_ind);
}

DSME_HANDLER(DSM_MSGTYPE_SAVE_DATA_IND, server, msg)
{
  emit_dsme_dbus_signal(dsme_save_unsaved_data_ind);
}

DSME_HANDLER(DSM_MSGTYPE_STATE_REQ_DENIED_IND, server, msg)
{
    const char* denied_request = shutdown_action_name(msg->state);

    dsme_log(LOG_WARNING,
             "proxying %s request denial due to %s to D-Bus",
             denied_request,
             (const char*)DSMEMSG_EXTRA(msg));

    DsmeDbusMessage* sig = dsme_dbus_signal_new(sig_path,
                                                sig_interface,
                                                dsme_state_req_denied_ind);
    dsme_dbus_message_append_string(sig, denied_request);
    dsme_dbus_message_append_string(sig, DSMEMSG_EXTRA(msg));

    dsme_dbus_signal_emit(sig);
}

DSME_HANDLER(DSM_MSGTYPE_DBUS_CONNECT, client, msg)
{
  dsme_log(LOG_DEBUG, "dbusproxy: DBUS_CONNECT");
  dsme_dbus_bind_methods(&bound, methods, service, req_interface);
}

DSME_HANDLER(DSM_MSGTYPE_DBUS_DISCONNECT, client, msg)
{
  dsme_log(LOG_DEBUG, "dbusproxy: DBUS_DISCONNECT");
  dsme_dbus_unbind_methods(&bound, methods, service, req_interface);
}

DSME_HANDLER(DSM_MSGTYPE_DSME_VERSION, server, msg)
{
  if (!dsme_version) {
      dsme_version = g_strdup(DSMEMSG_EXTRA(msg));
  }
}


module_fn_info_t message_handlers[] = {
  DSME_HANDLER_BINDING(DSM_MSGTYPE_STATE_CHANGE_IND),
  DSME_HANDLER_BINDING(DSM_MSGTYPE_BATTERY_EMPTY_IND),
  DSME_HANDLER_BINDING(DSM_MSGTYPE_SAVE_DATA_IND),
  DSME_HANDLER_BINDING(DSM_MSGTYPE_STATE_REQ_DENIED_IND),
  DSME_HANDLER_BINDING(DSM_MSGTYPE_DBUS_CONNECT),
  DSME_HANDLER_BINDING(DSM_MSGTYPE_DBUS_DISCONNECT),
  DSME_HANDLER_BINDING(DSM_MSGTYPE_DSME_VERSION),
  { 0 }
};


void module_init(module_t* handle)
{
  /* get dsme version so that we can report it over D-Bus if asked to */
  DSM_MSGTYPE_GET_VERSION req = DSME_MSG_INIT(DSM_MSGTYPE_GET_VERSION);
  broadcast_internally(&req);

  /* Do not connect to D-Bus; it is probably not started yet.
   * Instead, wait for DSM_MSGTYPE_DBUS_CONNECT.
   */

  dsme_log(LOG_DEBUG, "dbusproxy.so loaded");
}

void module_fini(void)
{
  dsme_dbus_unbind_methods(&bound, methods, service, req_interface);

  g_free(dsme_version);
  dsme_version = 0;

  dsme_log(LOG_DEBUG, "dbusproxy.so unloaded");
}
