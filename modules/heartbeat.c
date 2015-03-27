/**
   @file heartbeat.c

   Implements DSME server periodic wake up functionality.
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

#include "heartbeat.h"
#include "../include/dsme/modules.h"
#include "../include/dsme/logging.h"
#include "../include/dsme/mainloop.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include <glib.h>



static gboolean emit_heartbeat_message(GIOChannel*  source,
                                       GIOCondition condition,
                                       gpointer     data)
{
    // handle errors
    if (condition & (G_IO_ERR | G_IO_HUP | G_IO_NVAL)) {
        // the wd process has probably died; remove the watch & quit
        dsme_log(LOG_CRIT, "heartbeat: I/O error or HUP, terminating");
        dsme_main_loop_quit(EXIT_FAILURE);
        return false;
    }

    // first read the byte that woke us up
    ssize_t bytes_read;
    char    c;
    while ((bytes_read = read(STDIN_FILENO, &c, 1)) == -1 &&
           errno == EINTR)
    {
        // EMPTY LOOP
    }

    if (bytes_read == 1) {
        // got a ping from the wd process; respond with a pong
        ssize_t bytes_written;
        while ((bytes_written = write(STDOUT_FILENO, "*", 1)) == -1 &&
               (errno == EINTR))
        {
            // EMPTY LOOP
        }

        // send the heartbeat message
        const DSM_MSGTYPE_HEARTBEAT beat = DSME_MSG_INIT(DSM_MSGTYPE_HEARTBEAT);
        broadcast_internally(&beat);
        //dsme_log(LOG_DEBUG, "heartbeat");
        return true;
    } else {
        // got an EOF (or a read error); remove the watch
        dsme_log(LOG_DEBUG, "heartbeat: read() EOF or failure");
        return false;
    }
}

static bool start_heartbeat(void)
{
    // set up an I/O watch for the wake up pipe
    GIOChannel* chan  = 0;
    guint       watch = 0;

    if (!(chan = g_io_channel_unix_new(STDIN_FILENO))) {
        goto fail;
    }
    if (!(watch = g_io_add_watch(chan,
                                 G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL,
                                 emit_heartbeat_message,
                                 0)))
    {
        g_io_channel_unref(chan);
        goto fail;
    }
    g_io_channel_unref(chan);

    return true;


fail:
    return false;
}


void module_init(module_t* handle)
{
    dsme_log(LOG_DEBUG, "heartbeat.so loaded");

    start_heartbeat();
}

void module_fini(void)
{
    dsme_log(LOG_DEBUG, "heartbeat.so unloaded");
}
