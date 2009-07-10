/**
   @file usbtracker.c

   Track the alarm state from the alarm queue indications sent by alarmd.
   This is needed for device state selection by the state module.
   <p>
   Copyright (C) 2009 Nokia Corporation.

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

#include "state.h"
#include "dsme/modules.h"
#include "dsme/logging.h"
#include <glib.h>
#include <string.h>

#define DSME_SYSFS_USB_FILE "/sys/devices/platform/musb_hdrc/mode"

static gboolean handle_usb_state_change(GIOChannel*  source,
                                        GIOCondition condition,
                                        gpointer     data);
static void send_usb_state(const gchar* state_name);


bool  started = false;
guint watch;


static bool start_monitoring_sysfs()
{
    GIOChannel* chan;

    if ((chan = g_io_channel_new_file(DSME_SYSFS_USB_FILE, "r", 0)) &&
        (watch = g_io_add_watch(chan,
                                (G_IO_IN | G_IO_ERR),
                                handle_usb_state_change, 0)))
    {
        started = true;
    } else {
        g_io_channel_unref(chan);
        dsme_log(LOG_ERR, "g_io error trying to open %s", DSME_SYSFS_USB_FILE);
    }

    return started;
}

static gboolean handle_usb_state_change(GIOChannel*  source,
                                        GIOCondition condition,
                                        gpointer     data)
{
    bool keep = true;

    if (condition & G_IO_IN) {
        GError* error     = 0;
        gchar*  line      = 0;
        gsize   line_size = 0;

        g_io_channel_read_line(source, &line, &line_size, 0, &error);

        if (error) {
            dsme_log(LOG_ERR,
                     "Error reading from %s: %s",
                     DSME_SYSFS_USB_FILE,
                     error->message);
        } else if (line_size == 0 || line == 0) {
            dsme_log(LOG_ERR, "Empty read from %s", DSME_SYSFS_USB_FILE);
        } else {
            send_usb_state(line);
        }

        g_clear_error(&error);
    }

    if (condition & G_IO_ERR) {
        dsme_log(LOG_ERR, "g_io error monitoring %s", DSME_SYSFS_USB_FILE);
        keep    = false;
        started = false;
    }

    return keep;
}

static void send_usb_state(const gchar* state_name)
{
    DSM_MSGTYPE_SET_USB_STATE msg = DSME_MSG_INIT(DSM_MSGTYPE_SET_USB_STATE);

    if (strcmp(state_name, "b_peripheral") == 0) {
        msg.connected_to_pc = true;
    } else {
        msg.connected_to_pc = false;
    }

    dsme_log(LOG_DEBUG,
             "broadcasting usb state: %s",
             msg.connected_to_pc ? "connected" : "disconnected");
    broadcast_internally(&msg);
}

static void stop_monitoring_sysfs()
{
    if (started) {
        g_source_remove(watch);
        started = false;
    }
}


void module_init(module_t* handle)
{
    dsme_log(LOG_DEBUG, "libusbtracker.so loaded");

    start_monitoring_sysfs();
}

void module_fini()
{
    stop_monitoring_sysfs();

    dsme_log(LOG_DEBUG, "libusbtracker.so unloaded");
}
