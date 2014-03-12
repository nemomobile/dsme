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

#include <libngf/ngf.h>
#include <stdio.h>
#include <stdlib.h>

#include <glib.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>

#define PFIX "shutdownfeedback: "
#define NGF_PWROFF_EVENT "pwroff"
static NgfClient *ngf_client = NULL;
static DBusConnection  *dbus_connection = NULL;
static uint32_t playing_event_id = 0;

static void ngf_callback(NgfClient *client, uint32_t id, NgfEventState state, void *data);

static void create_ngf_client(void)
{
    if (ngf_client) {
        /* We already have a connection */
        return;
    }

    if (!dbus_connection) {
        dsme_log(LOG_WARNING, PFIX"No dbus connection. Can't connect to ngf");
        return;
    }

    if ((ngf_client = ngf_client_create(NGF_TRANSPORT_DBUS, dbus_connection)) == NULL) {
        dsme_log(LOG_ERR, PFIX"Can't create ngf client");
    } else {
        ngf_client_set_callback(ngf_client, ngf_callback, NULL);
    }
}

static void destroy_ngf_client(void)
{
    /* we should do something like this *
     * if (ngf_client) {
     *   ngf_client_destroy(ngf_client);
     *   ngf_client = NULL;
     *   playing_event_id = 0;
     * }
    * but shutdown is already going, ngfd is already down, same as dbus
    * so there is no point of doing clean destroy.
    * Let system go down and forget about destroy
    */
}

static void
ngf_callback(NgfClient *client, uint32_t event_id, NgfEventState state, void *userdata)
{
    (void) client;
    (void) userdata;
    const char *state_name;
    bool play_done = false;

    switch (state) {
        case NGF_EVENT_FAILED:
            state_name = "Failed";
            play_done = true;
            break;
        case NGF_EVENT_COMPLETED:
            state_name = "Completed";
            play_done = true;
            break;
        case NGF_EVENT_PLAYING:
            state_name = "Playing";   break;
        case NGF_EVENT_PAUSED:
            state_name = "Paused";    break;
        case NGF_EVENT_BUSY:
            state_name = "Busy";      break;
        case NGF_EVENT_LONG:
            state_name = "Long";      break;
        case NGF_EVENT_SHORT:
            state_name = "Short";     break;
        default:
            state_name = "Unknown";
            play_done = true;
            break;
    }
    dsme_log(LOG_DEBUG, PFIX"%s(%s, %d)", __FUNCTION__, state_name, event_id);

    if (play_done) {
        playing_event_id = 0;
    }
}

static void play_vibra(void)
{
    static char event[] = NGF_PWROFF_EVENT;

    if (playing_event_id) {
        /* We already are playing an event, don't start new one */
        // dsme_log(LOG_DEBUG, PFIX"Play already going, skip");
        return;
    }

    if (!ngf_client) {
        create_ngf_client();
    }
    if (!ngf_client) {
        dsme_log(LOG_ERR, PFIX"Can't play vibra. We don't have ngf client");
        return;
    }

    playing_event_id = ngf_client_play_event (ngf_client, event, NULL);
    dsme_log(LOG_DEBUG, PFIX"PLAY(%s, %d)", event, playing_event_id);
}


DSME_HANDLER(DSM_MSGTYPE_STATE_CHANGE_IND, conn, msg)
{
    if ((msg->state == DSME_STATE_SHUTDOWN) ||
        (msg->state == DSME_STATE_REBOOT)) {
        //dsme_log(LOG_DEBUG, PFIX"shutdown/reboot state received");
        play_vibra();
    }
}
DSME_HANDLER(DSM_MSGTYPE_REBOOT_REQ, conn, msg)
{
    // dsme_log(LOG_DEBUG, PFIX"reboot reques received");
    play_vibra();
}

DSME_HANDLER(DSM_MSGTYPE_SHUTDOWN_REQ, conn, msg)
{
    //dsme_log(LOG_DEBUG, PFIX"shutdown reques received");
    play_vibra();
}

DSME_HANDLER(DSM_MSGTYPE_DBUS_CONNECT, conn, msg)
{
    DBusError err = DBUS_ERROR_INIT;

    //dsme_log(LOG_INFO, PFIX"DBUS_CONNECT");
    if (!(dbus_connection = dsme_dbus_get_connection(&err))) {
        dsme_log(LOG_WARNING, PFIX"can't connect to systembus: %s: %s",
               err.name, err.message);
        goto cleanup;
    }
    dbus_connection_setup_with_g_main(dbus_connection, NULL);

cleanup:
    dbus_error_free(&err);
}

DSME_HANDLER(DSM_MSGTYPE_DBUS_DISCONNECT, conn, msg)
{
    //dsme_log(LOG_INFO, PFIX"DBUS_DISCONNECT");
    destroy_ngf_client();
    if (dbus_connection) {
        dbus_connection_unref(dbus_connection);
        dbus_connection = NULL;
    }
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
    dsme_log(LOG_DEBUG, "shutdownfeedback.so unloaded");
}
