/**
   @file mainloop.c

   Implements DSME mainloop functionality.
   <p>
   Copyright (C) 2004-2010 Nokia Corporation.

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
#include "../include/dsme/mainloop.h"
#include "../include/dsme/logging.h"

#include <stdbool.h>
#include <glib.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

typedef enum { NOT_STARTED, RUNNING, STOPPED } main_loop_state_t;

static volatile main_loop_state_t state    = NOT_STARTED;
static GMainLoop*                 the_loop = 0;
static int                        signal_pipe[2];
static int                        main_loop_exit_code = EXIT_SUCCESS;

static gboolean handle_signal(GIOChannel*  source,
                              GIOCondition condition,
                              gpointer     data);

static bool set_up_signal_pipe(void)
{
    /* create a non-blocking pipe for waking up the main thread */
    if (pipe(signal_pipe) == -1) {
        dsme_log(LOG_CRIT, "error creating wake up pipe: %s", strerror(errno));
        goto fail;
    }

    /* set writing end of the pipe to non-blocking mode */
    int flags;
    errno = 0;
    if ((flags = fcntl(signal_pipe[1], F_GETFL)) == -1 && errno != 0) {
        dsme_log(LOG_CRIT,
                 "error getting flags for wake up pipe: %s",
                 strerror(errno));
        goto close_and_fail;
    }
    if (fcntl(signal_pipe[1], F_SETFL, flags | O_NONBLOCK) == -1) {
        dsme_log(LOG_CRIT,
                 "error setting wake up pipe to non-blocking: %s",
                 strerror(errno));
        goto close_and_fail;
    }

    /* set up an I/O watch for the wake up pipe */
    GIOChannel* chan  = 0;
    guint       watch = 0;

    if (!(chan = g_io_channel_unix_new(signal_pipe[0]))) {
        goto close_and_fail;
    }
    watch = g_io_add_watch(chan, G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL,
			   handle_signal, 0);
    g_io_channel_unref(chan);
    if (!watch) {
        goto close_and_fail;
    }

    return true;


close_and_fail:
    (void)close(signal_pipe[1]);
    (void)close(signal_pipe[0]);

fail:
    return false;
}

static gboolean handle_signal(GIOChannel*  source,
                              GIOCondition condition,
                              gpointer     data)
{
    g_main_loop_quit(the_loop);
    return FALSE;
}


void dsme_main_loop_run(void (*iteration)(void))
{
    if (state == NOT_STARTED) {
        if (!(the_loop = g_main_loop_new(0, FALSE)) ||
            !set_up_signal_pipe())
        {
            // TODO: crash and burn
            exit(EXIT_FAILURE);
        }

        GMainContext* ctx = g_main_loop_get_context(the_loop);

        state = RUNNING;
        while (state == RUNNING) {
            if (iteration) {
                iteration();
            }
            if (state == RUNNING) {
                (void)g_main_context_iteration(ctx, TRUE);
            }
        }

        g_main_loop_unref(the_loop);
        the_loop = 0;
    }
}

void dsme_main_loop_quit(int exit_code)
{
    if (state == RUNNING) {
        state = STOPPED;

        dsme_log_stop();

        if (main_loop_exit_code < exit_code) {
            main_loop_exit_code = exit_code;
        }

        ssize_t bytes_written;
        while ((bytes_written = write(signal_pipe[1], "*", 1)) == -1 &&
               (errno == EINTR))
        {
            /* EMPTY LOOP */
        }
    }
}

int dsme_main_loop_exit_code(void)
{
    return main_loop_exit_code;
}

