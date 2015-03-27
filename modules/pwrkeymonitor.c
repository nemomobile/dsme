/**
   @file pwrkeymonitor.c

   @brief Power key monitor plugin for forced poweroff.

   Pwrkeymonitor is intended for "fallback poweroff", in case the UI (or other
   component) responsible for handling powerkey events has died or become
   unresponsible. In reading powerkey status, it currently only supports
   evdev interface of the kernel. Currently, the plugin searches for evdev
   drivers that can emit KEY_POWER events. It sends a shutdown event when
   powerkey has been continously pressed for 5 seconds.
   <p>
   Copyright (C) 2010 Nokia Corporation.
   Copyright (C) 2013 Jolla Ltd.

   @author Markus Lehtonen <markus.lehtonen@nokia.com>
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

#include "../include/dsme/modules.h"
#include "../include/dsme/logging.h"
#include "../include/dsme/mainloop.h"
#include "../include/dsme/timers.h"
#include "runlevel.h"
#include "state-internal.h"

#include <sys/ioctl.h>
#include <sys/types.h>
#include <linux/input.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <values.h>

#include <glib.h>

/** How long the the power key must be held before ui-less
 *  shutdown is started */
#define PWRKEY_TIMER_SECONDS 5

/** Timer for starting shutdown */
static dsme_timer_t pwrkey_timer = 0;

/** Prefix string for logging messages from this module */
#define PFIX "pwrkeymonitor: "

/** Predicate for: Operating system update in progress
 *
 * Determined from presence of update mode flag file.
 */
static bool pwrkey_update_mode_is_active(void)
{
    static const char flagfile[] = "/tmp/os-update-running";

    return access(flagfile, F_OK) == 0;
}

/** Timer callback function for initiating shutdown
 *
 * @param data (not used)
 *
 * @return FALSE to stop the timer from repeating
 */
static int
pwrkey_trigger(void *data)
{
    if( !pwrkey_timer )
    {
        /* Cancelled but got triggered anyway */
        return FALSE;
    }

    /* Invalidate cached timer id */
    pwrkey_timer = 0;

    /* No shutdown via powerkey while in update mode */
    if( pwrkey_update_mode_is_active() )
    {
        dsme_log(LOG_WARNING, PFIX"ongoing os update; ignoring power key");
        // TODO: send a dbus signal (TBD) so that ui side can warn the
        //       user before some hw specific immediate power off gets
        //       triggered if power key is kept pressed any longer
        return FALSE;
    }

    dsme_log(LOG_CRIT, PFIX"Timer triggered, initiating shutdown");

#ifdef PWRKEY_SHUTDOWN_MSG_TELINIT
    /* Use telinit message */
    const char runlevel[] = "SHUTDOWN";
    char* buf = NULL;
    DSM_MSGTYPE_TELINIT* msg = NULL;

    msg = DSME_MSG_NEW_WITH_EXTRA(DSM_MSGTYPE_TELINIT, sizeof(runlevel));
    buf = ((char *)msg) + sizeof(DSM_MSGTYPE_TELINIT);
    strcpy(buf, runlevel);
    broadcast_internally(msg);
    free(msg);
#else
    /* Use the "normal" shutdown request */
    DSM_MSGTYPE_SHUTDOWN_REQ msg = DSME_MSG_INIT(DSM_MSGTYPE_SHUTDOWN_REQ);
    broadcast_internally(&msg);
#endif

    /* No repeats */
    return FALSE;
}

/** Start shutdown timer
 */
static void
start_pwrkey_timer(void)
{
    if( pwrkey_timer )
    {
        dsme_log(LOG_DEBUG, PFIX"Timer already running");
    }
    else if( !(pwrkey_timer = dsme_create_timer(PWRKEY_TIMER_SECONDS,
                                                pwrkey_trigger, NULL)) )
    {
        dsme_log(LOG_CRIT, PFIX"Timer creation failed!");
    }
    else
    {
        dsme_log(LOG_DEBUG, PFIX"Timer started (%d sec to shutdown)",
                 PWRKEY_TIMER_SECONDS);
    }
}

/** Cancel shutdown timer
 */
static void
stop_pwrkey_timer(void)
{
    if (pwrkey_timer)
    {
        dsme_destroy_timer(pwrkey_timer);
        pwrkey_timer = 0;
        dsme_log(LOG_DEBUG, PFIX"Timer stopped");
    }
}

/** Calculate how many elements an array of longs bitmap needs to
 *  have enough space for bits items */
#define EVDEVBITS_LEN(bits) (((bits)+LONGBITS-1)/LONGBITS)

/** Calculate offset in unsigned long array for given bit */
#define EVDEVBITS_OFFS(bit) ((bit) / LONGBITS)

/** Calculate unsigned long mask for given bit */
#define EVDEVBITS_MASK(bit) (1ul << ((bit) % LONGBITS))

/** Check if the fd is an evdev node that emits power key events
 *
 * @param fd file descriptor to check
 *
 * @return true if power key events are emitted, false otherwise
 */
static bool
emits_powerkey_events(int fd)
{
    bool res = false;

    unsigned long keys[EVDEVBITS_LEN(KEY_CNT)];

    memset(keys, 0, sizeof keys);

    if( ioctl(fd, EVIOCGBIT(EV_KEY, KEY_CNT), keys) == -1 )
    {
        dsme_log(LOG_DEBUG, PFIX"EVIOCGBIT(%d): %m", fd);
    }
    else if( keys[EVDEVBITS_OFFS(KEY_POWER)] & EVDEVBITS_MASK(KEY_POWER) )
    {
        res = TRUE;
    }

    return res;
}

/** Structure for keeping track of io channels and watches */
typedef struct
{
    GIOChannel *chan;
    guint       watch;

} channel_watch_t;

/** Allocate and initialize an io channel watch structure
 *
 * @param chan io channel
 * @param watch io watch associated with the channel
 *
 * @return pointer io channel watch structure
 */
static
channel_watch_t *
channel_watch_create(GIOChannel *chan, guint watch)
{
    channel_watch_t *self = g_malloc0(sizeof *self);
    self->chan  = chan;
    self->watch = watch;
    return self;
}

/** Delete an io channel watch structure and free resources bound to it
 *
 * @param self pointer io channel watch structure
 * @param remove_watch should the io watch be removed too
 */
static
void
channel_watch_delete(channel_watch_t *self, bool remove_watch)
{
    if( self )
    {
        if( remove_watch && self->watch )
        {
             g_source_remove(self->watch);
        }
        if( self->chan )
        {
            g_io_channel_unref(self->chan);
        }
        g_free(self);
    }
}

/** List of io channels and watches tracking power button presses */
static GSList *watchlist = 0;

/** Append an io channel and a watch for it into list of things under tracking
 *
 * @param chan io channel
 * @param watch watch id for the channel
 */
static
void
watchlist_add(GIOChannel *chan, guint watch)
{
    watchlist = g_slist_prepend(watchlist, channel_watch_create(chan, watch));
}

/** Remove a io channel from the list of things under tracking
 *
 * Note: Does not remove the watch id associated with the channel.
 *
 * @param chan io channel
 */
static
void
watchlist_remove(GIOChannel *chan)
{
    GSList **pos = &watchlist;
    GSList *now;

    while( (now = *pos) )
    {
        channel_watch_t *cw = now->data;

        if( cw->chan != chan )
        {
            pos = &now->next;
            continue;
        }

        // unlink slot from list and free it
        *pos = now->next, now->next = 0;
        g_slist_free(now);

        channel_watch_delete(cw, false);
        break;
    }
}

/** Detach from all io channels that are open for powerkey tracking
 */
static
void
watchlist_clear(void)
{
    for( GSList *now = watchlist; now; now = now->next )
    {
        channel_watch_delete(now->data, true);
    }
    g_slist_free(watchlist), watchlist = 0;
}

/** Process evdev input events
 *
 * @param chan io channel that has input available
 * @param condition bitmask of input/error states
 * @param data (not used)
 *
 * return TRUE to keep the watch alive, or FALSE to remove it
 */
static
gboolean
process_kbevent(GIOChannel* chan, GIOCondition condition, gpointer data)
{
    gboolean keep_going = TRUE;

    struct input_event buf[32];

    /* Abandon watch if we get abnorman conditions from glib */
    if (condition & ~(G_IO_IN | G_IO_PRI))
    {
        dsme_log(LOG_ERR, PFIX"I/O error");
        keep_going = FALSE;
    }

    /* Do the actual reading with good old read() syscall */
    int fd = g_io_channel_unix_get_fd(chan);
    int rc = read(fd, buf, sizeof buf);

    if( rc < 0 )
    {
        switch( errno )
        {
        case EINTR:
        case EAGAIN:
            break;
        default:
            dsme_log(LOG_ERR, PFIX"read: %m");
            keep_going = FALSE;
            break;
        }
    }
    else if( rc == 0 )
    {
        dsme_log(LOG_ERR, PFIX"read: EOF");
        keep_going = FALSE;
    }
    else
    {
        int n = rc / sizeof *buf;

        dsme_log(LOG_DEBUG, PFIX"Processing %d events", n);

        for( int i = 0; i < n; ++i )
        {
            const struct input_event *eve = buf + i;

            dsme_log(LOG_DEBUG, PFIX"Got event, type: %d code: %d value: %d",
                     eve->type, eve->code, eve->value);

            if( eve->type == EV_KEY && eve->code == KEY_POWER )
            {
                switch( eve->value )
                {
                case 1: // pressed
                    start_pwrkey_timer();
                    break;
                case 0: // released
                    stop_pwrkey_timer();
                    break;
                default: // repeat ignored
                    break;
                }
            }
        }
    }

    if( !keep_going )
    {
        dsme_log(LOG_WARNING, PFIX"disabling io watch");

        /* the watch gets removed as we return FALSE, but
         * the channel must be removed from tracking list */
        watchlist_remove(chan);
    }

    return keep_going;
}

/** Probe an input device and track it if it can emit powerkey events
 *
 * @param path path of input device
 *
 * @return true if tracking was started succesfully, false if it was
 *         not a powerkey device or tracking could not be started
 */
static
bool
probe_evdev_device(const char *path)
{
    bool        res   = false;

    int         file  = -1;
    GIOChannel *chan  = 0;
    guint       watch = 0;
    GError     *err   = 0;

    if( (file = open(path, O_RDONLY)) == -1 )
    {
        dsme_log(LOG_ERR, PFIX"%s: open: %m", path);
        goto EXIT;
    }

    if( !emits_powerkey_events(file) )
    {
        dsme_log(LOG_DEBUG, PFIX"%s: not a powerkey device", path);
        goto EXIT;
    }

    if( !(chan = g_io_channel_unix_new(file)) )
    {
        dsme_log(LOG_ERR, PFIX"%s: io channel setup failed", path);
        goto EXIT;
    }

    /* io watch owns the file  */
    g_io_channel_set_close_on_unref(chan, true), file = -1;

    /* Set to NULL encoding so that we can turn off the buffering */
    if( g_io_channel_set_encoding(chan, NULL, &err) != G_IO_STATUS_NORMAL )
    {
        dsme_log(LOG_WARNING, PFIX"%s: unable to set io channel encoding",
                 path);
        if( err )
        {
             dsme_log(LOG_WARNING, PFIX"%s", err->message);
        }
        // continue anyway
    }
    g_io_channel_set_buffered(chan, false);

    if( !(watch = g_io_add_watch(chan,
				 G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL,
				 process_kbevent, 0)) )
    {
        dsme_log(LOG_ERR, PFIX"%s: unable to add io channel watch", path);
        goto EXIT;
    }

    dsme_log(LOG_DEBUG, PFIX"%s: channel=%p, watch=%u", path, chan, watch);

    /* watchlist owns the channel and watch */
    watchlist_add(chan, watch), chan = 0, watch = 0;

    res = true;

EXIT:
    g_clear_error(&err);

    if( watch !=  0 ) g_source_remove(watch);
    if( chan  !=  0 ) g_io_channel_unref(chan);
    if( file  != -1 ) close(file);

    return res;
}

/** Scan and track evdev sources that can emit powerkey events
 */
static bool
start_pwrkey_monitor(void)
{
    static const char base[] = "/dev/input";

    DIR *dir = 0;
    int  cnt = 0;

    char path[256];
    struct dirent *de;

    if( !(dir = opendir(base)) )
    {
        dsme_log(LOG_ERR, PFIX"opendir(%s): %m", base);
        goto EXIT;
    }

    while( (de = readdir(dir)) )
    {
        if( strncmp(de->d_name, "event", 5) )
        {
            continue;
        }

        snprintf(path, sizeof path, "%s/%s", base, de->d_name);

        if( probe_evdev_device(path) )
        {
            cnt += 1;
        }
    }

EXIT:

    if( dir ) closedir(dir);

    if( cnt < 1 )
    {
        dsme_log(LOG_WARNING, PFIX"could not find any powerkey input devices");
    }

    return cnt > 0;
}

/** Close any io channels still open for powerkey tracking
 */
static void
stop_pwrkey_monitor(void)
{
    watchlist_clear();
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
