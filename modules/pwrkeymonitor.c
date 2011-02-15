/**
   @file pwrkeymonitor.c

   @brief Power key monitor plugin for forced poweroff.

   Pwrkeymonitor is intended for "fallback poweroff", in case the UI (or other
   component) responsible for handling powerkey events has died or become
   unresponsible. In reading powerkey status, it currently only supports
   evdev interface of the kernel. The plugin is written for Nokia N900 but it
   could relatively easily be modified to support other platforms as well, name
   by adding run-time configurability. Currently, the plugin searches for evdev
   driver whose name includes substring "pwrbutton". It sends a shutdown event 
   powerkey has been continously pressed for 5 seconds.
   <p>
   Copyright (C) 2010 Nokia Corporation.

   @author Markus Lehtonen <markus.lehtonen@nokia.com>

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

#include "dsme/modules.h"
#include "dsme/logging.h"
#include "dsme/mainloop.h"
#include "dsme/timers.h"
#include "runlevel.h"
#include "state-internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gprintf.h>
#include <glib/gstdio.h>

#include <linux/input.h>

#define EVDEV_DEV_DIR_PATH "/dev/input/"
#define EVDEV_NAME_MATCH "pwrbutton"
#define EVDEV_FALLBACK_DEV_PATH "/dev/input/event3"
#define PWRKEY_TIMER_SECONDS 5

/* Use nonblock read. This is safer as it ensures that the read of event data
 * will newer block and, thus, the execution of dsme-server is newer stalled 
 * there.
 */
#define PWRKEY_READ_NONBLOCK 1

/* TODO: Run-time configurability (configuration file?) for pwrkeymonitor.
 *       Options to configure: device name, device path, timer timeout
 * TODO: Fallback device path is not used for anything
 */

static dsme_timer_t pwrkey_timer = 0;
static GIOChannel *evdev_chan = NULL;
static guint evdev_watch = 0;

static int
match_device(const gchar * devdir, const gchar * namematch)
{
    GDir *dir = NULL;
    const gchar *filename = NULL;
    GError *error = NULL;
    gchar devname[256] = "";

    dir = g_dir_open(devdir, 0, &error);
    if (dir == NULL)
    {
        dsme_log(LOG_WARNING, "pwrkeymonitor: Unable to open device dir");
        goto err;
    }

    while ((filename = g_dir_read_name(dir)))
    {
        if (!g_file_test
            (filename, G_FILE_TEST_IS_SYMLINK | G_FILE_TEST_IS_DIR))
        {
            int fd;
            int ret;
            gchar *abspath;

            abspath = g_strjoin("/", devdir, filename, NULL);

            dsme_log(LOG_DEBUG,
                     "pwrkeymonitor: Trying dev \"%s\"...", abspath);
            fd = g_open(abspath, O_RDONLY);

            if (fd <= 0)
            {
                dsme_log(LOG_DEBUG,
                         "pwrkeymonitor: Unable to open file (\"%s\")",
                         abspath);
                g_free(abspath);
                continue;
            }

            ret = ioctl(fd, EVIOCGNAME(sizeof(devname)), devname);
            if (ret == -1)
            {
                dsme_log(LOG_DEBUG,
                         "pwrkeymonitor: Unable to read device name of %s",
                         abspath);
            }
            else if (g_strstr_len(devname, sizeof(devname), namematch))
            {
                dsme_log(LOG_INFO,
                         "pwrkeymonitor: Matched device name \"%s\" (\"%s\"in %s)",
                         namematch, devname, abspath);
                g_dir_close(dir);
                g_free(abspath);
                return fd;
            }
            dsme_log(LOG_DEBUG,
                     "pwrkeymonitor: No match, devname was \"%s\"...",
                     devname);
            g_free(abspath);
            close(fd);
        }
    }
    dsme_log(LOG_INFO,
             "pwrkeymonitor: Unable to find device that matches \"%s\"",
             namematch);

    g_dir_close(dir);

  err:
    return 0;
}

static int
pwrkey_trigger(void* foo)
{
/* Use telinit message */
#ifdef PWRKEY_SHUTDOWN_MSG_TELINIT
    const char runlevel[] = "SHUTDOWN";
    char* buf = NULL;
    DSM_MSGTYPE_TELINIT* msg = NULL;

    msg = DSME_MSG_NEW_WITH_EXTRA(DSM_MSGTYPE_TELINIT, sizeof(runlevel));
    buf = ((char *)msg) + sizeof(DSM_MSGTYPE_TELINIT);
    strcpy(buf, runlevel);
    broadcast_internally(msg);
    free(msg);
/* Use the "normal" shutdown request */
#else
    DSM_MSGTYPE_SHUTDOWN_REQ msg = DSME_MSG_INIT(DSM_MSGTYPE_SHUTDOWN_REQ);
    broadcast_internally(&msg);
#endif

    return 0;
}

static gboolean
start_pwrkey_timer()
{
    if (!pwrkey_timer)
    {
        if (!(pwrkey_timer = dsme_create_timer(PWRKEY_TIMER_SECONDS,
                                               pwrkey_trigger, NULL)))
        {
            dsme_log(LOG_CRIT, "pwrkeymonitor: Timer creation failed!");
            return false;
        }
        dsme_log(LOG_DEBUG,
                 "pwrkeymonitor: Timer started (%d sec to shutdown)",
                 PWRKEY_TIMER_SECONDS);
    }
    else
    {
        dsme_log(LOG_DEBUG, "pwrkeymonitor: Timer already running");
    }
    return true;
}

static void
stop_pwrkey_timer()
{
    if (pwrkey_timer)
    {
        dsme_destroy_timer(pwrkey_timer);
        pwrkey_timer = 0;
        dsme_log(LOG_DEBUG, "pwrkeymonitor: Timer stopped");
    }
}

static gboolean
process_kbevent(GIOChannel* source, GIOCondition condition, gpointer data)
{
    /* Handle errors */
    if (condition & ~(G_IO_IN | G_IO_PRI))
    {
        dsme_log(LOG_DEBUG, "pwrkeymonitor: I/O error");
        return false;
    }
    //dsme_log(LOG_DEBUG, "pwrkeymonitor: Event handler called...");

    /* first read the byte that woke us up */
    GIOStatus ret;
    struct input_event ev;
    gsize bytes = 0;
    GError *error = NULL;

    do
    {
        ret = g_io_channel_read_chars(source,
                                      (gchar *) &ev,
                                      sizeof(struct input_event),
                                      (gsize *) &bytes,
                                      &error);
        //dsme_log(LOG_DEBUG, "pwrkeymonitor: read %d bytes", bytes);
        if (ret != G_IO_STATUS_NORMAL && ret != G_IO_STATUS_AGAIN)
        {
            /* We get G_IO_STATUS_AGAIN when there is no more data to read.
             * So that is kind of normal condition
             */
            dsme_log(LOG_WARNING,
                    "pwrkeymonitor: error reading evdev data");
        }
        if (bytes == sizeof(struct input_event))
        {
            dsme_log(LOG_DEBUG,
                    "pwrkeymonitor: Got event, type: %d code: %d value: %d",
                    ev.type, ev.code, ev.value);

            if (ev.type == EV_KEY && ev.code == KEY_POWER)
            {
                if (ev.value == 1)
                    start_pwrkey_timer();
                else
                    stop_pwrkey_timer();
            }
        }
        else if (bytes != 0)
        {
            dsme_log(LOG_WARNING,
                     "pwrkeymonitor: evdev data alignment mismatch \
                     (got %d bytes of %d expected)",
                     bytes, sizeof(struct input_event));
        }
    }
/* In nonblocking mode, read as long as there is data to read */
#ifdef PWRKEY_READ_NONBLOCK
    while (bytes > 0);
/* In blocking mode, read only one ev */
#else
    while (0);
#endif

    /* Don't ever remove, Watch is only removed in stop_pwrkey_monitor() */
    return true;
}

static bool
start_pwrkey_monitor(void)
{
    GIOChannel *chan = NULL;
    GError *error = NULL;
    guint watch = 0;
    int fd = 0;

    /* Find and open device */
    fd = match_device(EVDEV_DEV_DIR_PATH, EVDEV_NAME_MATCH);
    if (fd <= 0)
    {
        goto err2;
    }
    if (!(chan = g_io_channel_unix_new(fd)))
    {
        dsme_log(LOG_ERR, "pwrkeymonitor: unable to set up io channel");
        goto err1;
    }
    if (g_io_channel_set_encoding(chan, NULL, &error) != G_IO_STATUS_NORMAL)
    {
        dsme_log(LOG_ERR,
                 "pwrkeymonitor: unable to set I/O channel to raw mode");
        g_io_channel_unref(chan);
        goto err1;
    }
    g_io_channel_set_buffered(chan, false);
#ifdef PWRKEY_READ_NONBLOCK
    if (g_io_channel_set_flags(chan, G_IO_FLAG_NONBLOCK, &error) !=
        G_IO_STATUS_NORMAL)
    {
        dsme_log(LOG_ERR,
                 "pwrkeymonitor: unable to set I/O channel non-blocking mode");
        g_io_channel_unref(chan);
        goto err1;
    }
#endif

    /* Set up an I/O watch for the device */
    if (!(watch = g_io_add_watch(chan,
                                 (G_IO_PRI | G_IO_IN | G_IO_ERR),
                                 process_kbevent, 0)))
    {
        dsme_log(LOG_ERR, "pwrkeymonitor: unable to set up io watch");
        g_io_channel_unref(chan);
        goto err1;
    }
    evdev_chan = chan;
    evdev_watch = watch;

    dsme_log(LOG_DEBUG, "pwrkeymonitor: I/O channel set up successfully");
    return true;

  err1:
    close(fd);
  err2:
    return false;
}

static void
stop_pwrkey_monitor(void)
{
    g_source_remove(evdev_watch);
    g_io_channel_unref(evdev_chan);
    stop_pwrkey_timer();
}


void
module_init(module_t * handle)
{
    start_pwrkey_monitor();

    dsme_log(LOG_DEBUG, "libpwrkeymonitor.so loaded");
}

void
module_fini(void)
{
    stop_pwrkey_monitor();
    dsme_log(LOG_DEBUG, "libpwrkeymonitor.so unloaded");
}
