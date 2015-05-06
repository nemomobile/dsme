/**
   @file vibrafeedback.c

   Play vibra using ngfd

   <p>
   Copyright (C) 2014 Jolla Oy.

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

#include "../include/dsme/modules.h"
#include "../include/dsme/logging.h"

#include <dsme/state.h>
#include <dsme/protocol.h>

#include <libngf/ngf.h>
#include <stdio.h>
#include <stdlib.h>

#include <glib.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "vibrafeedback.h"

#define PFIX "vibrafeedback: "
static NgfClient *ngf_client = NULL;
static DBusConnection  *dbus_connection = NULL;
static uint32_t playing_event_id = 0;

static void ngf_callback(NgfClient *client, uint32_t id, NgfEventState state, void *data);

static void create_ngf_client(void)
{
    //dsme_log(LOG_DEBUG, PFIX"%s()", __FUNCTION__);

    if (ngf_client) {
        dsme_log(LOG_DEBUG, PFIX"%s() %s", __FUNCTION__, "We already have a connection");
        return;
    }

    if (!dbus_connection) {
        dsme_ini_vibrafeedback();
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
    //dsme_log(LOG_DEBUG, PFIX"%s()", __FUNCTION__);
    if (ngf_client) {
        ngf_client_destroy(ngf_client);
        ngf_client = NULL;
        playing_event_id = 0;
    }
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
            dsme_log(LOG_ERR, PFIX"Failed to play id %d", event_id);
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

void dsme_play_vibra(const char *event_name)
{

    if (playing_event_id) {
        /* We already are playing an event, don't start new one */
        dsme_log(LOG_DEBUG, PFIX"Play already going, skip");
        return;
    }

    if (!ngf_client) {
        create_ngf_client();
    }
    if (!ngf_client) {
        dsme_log(LOG_ERR, PFIX"Can't play vibra. We don't have ngf client");
        return;
    }

    playing_event_id = ngf_client_play_event (ngf_client, event_name, NULL);
    dsme_log(LOG_DEBUG, PFIX"PLAY(%s, %d)", event_name, playing_event_id);
}

void dsme_ini_vibrafeedback(void) {

    DBusError err = DBUS_ERROR_INIT;

    dsme_log(LOG_DEBUG, PFIX"%s()", __FUNCTION__);
    if (!(dbus_connection = dsme_dbus_get_connection(&err))) {
        dsme_log(LOG_WARNING, PFIX"can't connect to systembus: %s: %s",
                 err.name, err.message);
        goto cleanup;
    }
    dbus_connection_setup_with_g_main(dbus_connection, NULL);
cleanup:
    dbus_error_free(&err);
}

void dsme_fini_vibrafeedback(void) {

    dsme_log(LOG_DEBUG, PFIX"%s()", __FUNCTION__);
    destroy_ngf_client();
    if (dbus_connection) {
        dbus_connection_unref(dbus_connection);
        dbus_connection = NULL;
    }
}
