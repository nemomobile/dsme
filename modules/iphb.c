/**
   @brief IP heartbeat service dsme plug-in

   @file iphbd.c

   IP heartbeat service dsme plug-in

   <p>
   Copyright (C) 2010 Nokia. All rights reserved.

   @author Raimo Vuonnala <raimo.vuonnala@nokia.com>
   @author Semi Malinen <semi.malinen@nokia.com>
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

#define _ISOC99_SOURCE
#define _GNU_SOURCE

#include <iphbd/libiphb.h>
#include <iphbd/iphb_internal.h>

#include "dbusproxy.h"
#include "dsme_dbus.h"
#include "heartbeat.h"
#include "../include/dsme/modules.h"
#include "../include/dsme/modulebase.h"
#include "../include/dsme/logging.h"
#include "../include/dsme/timers.h"
#include "../dsme/dsme-wdd-wd.h"

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/time.h>
#include <time.h>
#include <glib.h>
#include <sys/ioctl.h>
#include <linux/rtc.h>
#include <limits.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <mce/dbus-names.h>

#include "../include/android/android_alarm.h"

/* ------------------------------------------------------------------------- *
 * Constants
 * ------------------------------------------------------------------------- */

/** Prefix string for diagnostic messages from this module */
#define PFIX "IPHB: "

/** Maximum number of epoll events to process in one go */
#define DSME_MAX_EPOLL_EVENTS   10

/** How long it takes to power up to act dead mode to show alarms */
#define STARTUP_TIME_ESTIMATE_SECS 60

/** How long it takes to shutdown the device */
#define SHUTDOWN_TIME_ESTIMATE_SECS 60

/** How long we give mce time to take over RTC wakeup wakelock
 *
 * The time is specified in milliseconds, or -1 for no timeout
 */
#define RTC_WAKEUP_TIMEOUT_MS 2000

/** Maximum time to stay in suspend [seconds]; zero for no limit */
#define RTC_MAXIMUM_WAKEUP_TIME (30*60) // 30 minutes

/** Image create time = mtime of mer-release file */
#define IMAGE_TIME_STAMP_FILE "/etc/mer-release"

/** Saved system time = mtime of saved-time file */
#define SAVED_TIME_FILE       "/var/tmp/saved-time"

/** Where to persistently store alarm state data received from timed */
#define XTIMED_STATE_FILE     "/var/lib/dsme/timed_state"

/* ------------------------------------------------------------------------- *
 * Custom types
 * ------------------------------------------------------------------------- */

/** @brief  Allocated structure of one client in the linked client list in iphbd
 */
typedef struct _client_t {
    int               fd;      /*!< IPC (Unix domain) socket or -1 */
    endpoint_t       *conn;    /*!< internal client endpoint (if fd == -1) */
    void             *data;    /*!< internal client cookie (if fd == -1) */
    char             *pidtxt;  /*!< Client description, for debugging */
    struct timeval    reqtime; /*!< {0,0} if client has not subscribed to wake-up call */
    struct timeval    mintime; /*!< min end of sleep period */
    struct timeval    maxtime; /*!< max end of sleep period */
    pid_t             pid;     /*!< client process ID */
    bool              wakeup;  /*!< resume to handle */
    struct _client_t *next;    /*!< pointer to the next client in the list (NULL if none) */
} client_t;

/* ------------------------------------------------------------------------- *
 * Function prototypes
 * ------------------------------------------------------------------------- */

static void clientlist_wakeup_clients_now(const struct timeval *now);
static void clientlist_wakeup_clients_later(const struct timeval *now);
static void clientlist_wakeup_clients_cancel(void);

static bool epollfd_add_fd(int fd, void *ptr);
static void epollfd_remove_fd(int fd);

static void systemtime_init(void);

static char *tm_repr(const struct tm *tm, char *buff, size_t size);
static char *t_repr(time_t t, char *buff, size_t size);

/* ------------------------------------------------------------------------- *
 * Variables
 * ------------------------------------------------------------------------- */

/** Path to android alarm device node */
static const char android_alarm_path[] = "/dev/alarm";

/** File descriptor for android alarm device node */
static int android_alarm_fd = -1;

/** Handle to the epoll instance */
static int epollfd  = -1;

/** I/O watch for epollfd */
static guint epoll_watch = 0;

/** IPC client listen/accept handle */
static int listenfd = -1;

/** Handle to the kernel */
static int kernelfd = -1;

/** Path to RTC device node */
static const char rtc_path[] = "/dev/rtc0";

/** File descriptor for RTC device node */
static int rtc_fd = -1;

/** Linked lits of connected clients */
static client_t *clients = NULL;

/** Timer for serving wakeups with shorter than heartbeat range */
static guint wakeup_timer = 0;

/** System bus connection (for IPC with mce) */
static DBusConnection *systembus = 0;

/** Status of com.nokia.mce on systembus */
static bool mce_is_running = false;

/** Sysfs entry for acquiring wakelocks */
static const char lock_path[]   = "/sys/power/wake_lock";

/** Sysfs entry for releasing wakelocks */
static const char unlock_path[] = "/sys/power/wake_unlock";

/** RTC wakeup wakelock - acquired by dsme and released by mce / timeout */
static const char rtc_wakeup[] = "mce_rtc_wakeup";

/** RTC input wakelock - acquired / released by dsme */
static const char rtc_input[] = "dsme_rtc_input";

/** IPHB delayed wakeup processing */
static const char iphb_wakeup[] = "dsme_iphb_wakeup";

/** When the next alarm that should powerup/resume the device is due [systime] */
static time_t alarm_powerup = 0;

/** When the next alarm that should resume the device is due [systime] */
static time_t alarm_resume  = 0;

/* ------------------------------------------------------------------------- *
 * Generic utility functions
 * ------------------------------------------------------------------------- */

/** Helper for testing if tv1 < tv2
 *
 * @param tv1 time value
 * @param tv2 time value
 *
 * @return true if tv1 < tv2, false otherwise
 */
static inline bool tv_lt(const struct timeval *tv1, const struct timeval *tv2)
{
    return timercmp(tv1, tv2, <);
}

/** Helper for testing if tv1 > tv2
 *
 * @param tv1 time value
 * @param tv2 time value
 *
 * @return true if tv1 > tv2, false otherwise
 */
static inline bool tv_gt(const struct timeval *tv1, const struct timeval *tv2)
{
    return timercmp(tv1, tv2, >);
}

/** Helper for testing if tv1 >= tv2
 *
 * @param tv1 time value
 * @param tv2 time value
 *
 * @return true if tv1 >= tv2, false otherwise
 */
static inline bool tv_ge(const struct timeval *tv1, const struct timeval *tv2)
{
    return !tv_lt(tv1, tv2);
}

/** Helper for getting monotonic time as struct timeval
 *
 * @param tv place to store the monotonic time
 *
 * @return true on success, or false on failure
 */
static bool monotime_get_tv(struct timeval *tv)
{
    bool res = false;

    struct timespec ts;

#if defined(CLOCK_BOOTTIME)
    if( clock_gettime(CLOCK_BOOTTIME, &ts) < 0 ) {
        if( clock_gettime(CLOCK_MONOTONIC, &ts) < 0 )
            timerclear(tv);
    }
#else
    if( clock_gettime(CLOCK_MONOTONIC, &ts) < 0 )
	timerclear(tv);
#endif
    else {
	TIMESPEC_TO_TIMEVAL(tv, &ts);
	res = true;
    }
    return res;
}

/** Helper for getting system time as struct timeval
 *
 * @param tv place to store the system time
 *
 * @return true on success, or false on failure
 */
static bool realtime_get_tv(struct timeval *tv)
{
    bool res = false;

    if( gettimeofday(tv, 0) < 0 )
	timerclear(tv);
    else
	res = true;

    return res;
}

/** Emit wakeup time stamp to log
 *
 * @param lev   logging level
 * @param title string to emit before time stamp
 * @param t     time stamp to print
 * @param now   time now
 */
static void log_time_t(int lev, const char *title, time_t t, time_t now)
{
    struct tm tm;
    char left[32];

    if( t <= 0 ) {
	dsme_log(lev, PFIX"%s: not set", title);
	goto cleanup;
    }

    snprintf(left, sizeof left, " (T%+ld)", (long)(now - t));

    memset(&tm, 0, sizeof tm);
    gmtime_r(&t, &tm);

    dsme_log(lev, PFIX"%s: %04d-%02d-%02d %02d:%02d:%02d%s",
	     title,
	     tm.tm_year + 1900,
	     tm.tm_mon + 1,
	     tm.tm_mday,
	     tm.tm_hour,
	     tm.tm_min,
	     tm.tm_sec,
	     left);
cleanup:
    return;
}

/* ------------------------------------------------------------------------- *
 * ipc with hwwd kicker process
 * ------------------------------------------------------------------------- */

/** Tell hwwd kicker process that we're still alive */
static void hwwd_feeder_sync(void)
{
    /* The parent process is hwwd kicker, and the SIGHUP will interrupt
     * the nanosleep() it is most likely at */
    kill(getppid(), SIGHUP);
}

/* ------------------------------------------------------------------------- *
 * Utilities for manipulating wakelocks
 * ------------------------------------------------------------------------- */

/** Whether the wakelock sysfs paths are available
 *
 * @return true if wakelocks are supported, false otherwise
 */
static bool wakelock_supported(void)
{
    static bool checked = false;
    static bool supported = false;

    if( !checked ) {
	checked = true;
	supported = (access(lock_path, W_OK) == 0 &&
		     access(unlock_path, W_OK) == 0);
    }

    return supported;
}

/** Helper for writing to sysfs files
 *
 * @param path file to write to
 * @param data string to write
 */
static void wakelock_write(const char *path, const char *data, int ignore)
{
    int file;

    if( (file = TEMP_FAILURE_RETRY(open(path, O_WRONLY))) == -1 ) {
	dsme_log(LOG_WARNING, PFIX"%s: open: %m", path);
    }
    else {
	int size = strlen(data);
	errno = 0;
	if( TEMP_FAILURE_RETRY(write(file, data, size)) != size ) {
	    if( errno != ignore )
		dsme_log(LOG_WARNING, PFIX"%s: write: %m", path);
	}
	if( TEMP_FAILURE_RETRY(close(file)) == -1 ) {
	    dsme_log(LOG_WARNING, PFIX"%s: close: %m", path);
	}
    }
}

/** Use sysfs interface to create and enable a wakelock.
 *
 * @param name The name of the wakelock to obtain
 * @param ms   Time in milliseconds before the wakelock gets released
 *             automatically, or negative value for no timeout.
 */
static void wakelock_lock(const char *name, int ms)
{
    dsme_log(LOG_DEBUG, PFIX"LOCK: %s %d", name, ms);
    if( wakelock_supported() ) {
	char tmp[256];
	if( ms < 0 ) {
	    snprintf(tmp, sizeof tmp, "%s\n", name);
	}
	else {
	    long long ns = ms * 1000000LL;
	    snprintf(tmp, sizeof tmp, "%s %lld\n", name, ns);
	}
	wakelock_write(lock_path, tmp, -1);
    }
}

/** Use sysfs interface to disable a wakelock.
 *
 * @param name The name of the wakelock to release
 *
 * Note: This will not delete the wakelocks.
 */
static void wakelock_unlock(const char *name)
{
    dsme_log(LOG_DEBUG, PFIX"UNLK: %s", name);
    if( wakelock_supported() ) {
	char tmp[256];
	snprintf(tmp, sizeof tmp, "%s\n", name);
	/* assume EINVAL == the wakelock did not exist */
	wakelock_write(unlock_path, tmp, EINVAL);
    }
}

/* ------------------------------------------------------------------------- *
 * D-Bus helper functions
 * ------------------------------------------------------------------------- */

/** Create a GetNameOwner method call message
 *
 * @param name the dbus name to query
 *
 * @return DBusMessage pointer
 */
static
DBusMessage *
xdbus_create_name_owner_req(const char *name)
{
    DBusMessage *req = 0;

    req = dbus_message_new_method_call(DBUS_SERVICE_DBUS,
				       DBUS_PATH_DBUS,
				       DBUS_INTERFACE_DBUS,
				       "GetNameOwner");
    dbus_message_append_args(req,
			     DBUS_TYPE_STRING, &name,
			     DBUS_TYPE_INVALID);

    return req;
}

/** Parse a reply message to GetNameOwner method call
 *
 * @param rsp method call reply message
 *
 * @return dbus name of the name owner, or NULL in case of errors
 */
static
gchar *
xdbus_parse_name_owner_rsp(DBusMessage *rsp)
{
    char     *res = 0;
    DBusError err = DBUS_ERROR_INIT;
    char     *dta = NULL;

    if( dbus_set_error_from_message(&err, rsp) ||
	!dbus_message_get_args(rsp, &err,
			       DBUS_TYPE_STRING, &dta,
			       DBUS_TYPE_INVALID) ) {
	if( strcmp(err.name, DBUS_ERROR_NAME_HAS_NO_OWNER) ) {
	    dsme_log(LOG_WARNING, PFIX"%s: %s", err.name, err.message);
	}
	goto cleanup;
    }

    res = g_strdup(dta);

cleanup:
    dbus_error_free(&err);
    return res;
}

/* ------------------------------------------------------------------------- *
 * IPC with MCE
 * ------------------------------------------------------------------------- */

/** Change availability of mce on system bus status
 *
 * @param running whether com.nokia.mce has an owner or not
 */
static void xmce_set_runstate(bool running)
{
    if( mce_is_running != running ) {
	mce_is_running = running;
	dsme_log(LOG_NOTICE, PFIX"mce state -> %s",
		 running ? "running" : "terminated");
    }
}

/** Call back for handling asynchronous client verification via GetNameOwner
 *
 * @param pending   control structure for asynchronous d-bus methdod call
 * @param user_data (unused)
 */
static
void
xmce_verify_name_cb(DBusPendingCall *pending, void *user_data)
{
    (void)user_data;

    gchar       *owner = 0;
    DBusMessage *rsp   = 0;

    if( !(rsp = dbus_pending_call_steal_reply(pending)) )
	goto cleanup;

    owner = xdbus_parse_name_owner_rsp(rsp);
    xmce_set_runstate(owner && *owner);

cleanup:
    g_free(owner);

    if( rsp ) dbus_message_unref(rsp);
}

/** Verify that a mce exists via an asynchronous GetNameOwner method call
 *
 * @return true if the method call was initiated, or false in case of errors
 */
static
bool
xmce_verify_name(void)
{
    bool             res  = false;
    DBusMessage     *req  = 0;
    DBusPendingCall *pc   = 0;
    const char      *name = MCE_SERVICE;

    if( !systembus )
	goto cleanup;

    if( !(req = xdbus_create_name_owner_req(name)) )
	goto cleanup;

    if( !dbus_connection_send_with_reply(systembus, req, &pc, -1) )
	goto cleanup;

    if( !pc )
        goto cleanup;

    if( !dbus_pending_call_set_notify(pc, xmce_verify_name_cb, 0, 0) )
	goto cleanup;

    res = true;

cleanup:

    if( pc  ) dbus_pending_call_unref(pc);
    if( req ) dbus_message_unref(req);

    return res;
}

/** Signal cpu-keepalive plugin at mce side that rtc wakeup has occurred
 *
 * @return true if method call was sent, false in case of errors
 */
static bool xmce_cpu_keepalive_wakeup(void)
{
    bool         res = false;
    DBusMessage *req = 0;

    if( !systembus )
	goto cleanup;

    if( !mce_is_running )
	goto cleanup;

    req = dbus_message_new_method_call(MCE_SERVICE,
				       MCE_REQUEST_PATH,
				       MCE_REQUEST_IF,
				       MCE_CPU_KEEPALIVE_WAKEUP_REQ);
    if( !req )
	goto cleanup;

    dbus_message_set_auto_start(req, false);
    dbus_message_set_no_reply(req, true);

    if( !dbus_connection_send(systembus, req, 0) ) {
	dsme_log(LOG_WARNING, PFIX"failed to send %s.%s", MCE_REQUEST_IF,
		 MCE_CPU_KEEPALIVE_WAKEUP_REQ);
	goto cleanup;
    }

    res = true;

cleanup:

    if( req ) dbus_message_unref(req);

    return res;
}

/** D-Bus message filter for handling mce NameOwnerChanged signals
 *
 * @param con       dbus connection
 * @param msg       message to be acted upon
 * @param user_data (not used)
 *
 * @return DBUS_HANDLER_RESULT_NOT_YET_HANDLED (other filters see the msg too)
 */
static
DBusHandlerResult
xmce_dbus_filter_cb(DBusConnection *con, DBusMessage *msg, void *user_data)
{
    (void)user_data;

    DBusHandlerResult res = DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    const char *sender = 0;
    const char *object = 0;

    const char *name = 0;
    const char *prev = 0;
    const char *curr = 0;

    DBusError err = DBUS_ERROR_INIT;

    if( con != systembus )
	goto cleanup;

    if( !dbus_message_is_signal(msg, DBUS_INTERFACE_DBUS, "NameOwnerChanged") )
	goto cleanup;

    sender = dbus_message_get_sender(msg);
    if( strcmp(sender, DBUS_SERVICE_DBUS) )
	goto cleanup;

    object = dbus_message_get_path(msg);
    if( strcmp(object, DBUS_PATH_DBUS) )
	goto cleanup;

    if( !dbus_message_get_args(msg, &err,
			       DBUS_TYPE_STRING, &name,
			       DBUS_TYPE_STRING, &prev,
			       DBUS_TYPE_STRING, &curr,
			       DBUS_TYPE_INVALID) ) {
	dsme_log(LOG_WARNING, PFIX"%s: %s", err.name, err.message);
	goto cleanup;
    }

    if( !strcmp(name, MCE_SERVICE) ) {
	xmce_set_runstate(*curr != 0);
    }

cleanup:
    dbus_error_free(&err);
    return res;
}

/** Signal matching rule for mce name ownership changes */
static const char xmce_name_owner_match[] =
"type='signal'"
",sender='"DBUS_SERVICE_DBUS"'"
",interface='"DBUS_INTERFACE_DBUS"'"
",member='NameOwnerChanged'"
",path='"DBUS_PATH_DBUS"'"
",arg0='"MCE_SERVICE"'"
;

/** Start tracking mce state on systembus
 */
static void xmce_handle_dbus_connect(void)
{
    /* Register signal handling filter */
    dbus_connection_add_filter(systembus, xmce_dbus_filter_cb, 0, 0);

    /* NULL error -> match will be added asynchronously */
    dbus_bus_add_match(systembus, xmce_name_owner_match, 0);

    xmce_verify_name();
}

/** Stop tracking mce state on systembus
 */
static void xmce_handle_dbus_disconnect(void)
{
    /* NULL error -> match will be removed asynchronously */
    dbus_bus_remove_match(systembus, xmce_name_owner_match, 0);

    /* Remove signal handling filter */
    dbus_connection_remove_filter(systembus, xmce_dbus_filter_cb, 0);
}

/* ------------------------------------------------------------------------- *
 * Android alarm management functionality
 * ------------------------------------------------------------------------- */

/** Open android alarm device
 */
static void android_alarm_init(void)
{
    if( (android_alarm_fd = open(android_alarm_path, O_RDWR)) == -1 ) {
	/* Using /dev/alarm is optional; do not complain if it is missing */
	if( errno != ENOENT )
	    dsme_log(LOG_WARNING, PFIX"%s: %m", android_alarm_path);
    }
}

/** Close android alarm device
 */
static void android_alarm_quit(void)
{
    if( android_alarm_fd != -1 )
	close(android_alarm_fd), android_alarm_fd = -1;
}

static time_t android_alarm_prev = -1;

/** Add rtc wakeup via android alarm device
 *
 * @param t time stamp of the wakeup
 */
static void android_alarm_set(time_t delay)
{
    struct timespec now = { .tv_sec = 0, .tv_nsec = 0 };

    if( android_alarm_fd == -1 )
	goto cleanup;

    char tmp[32];

    int get_time = ANDROID_ALARM_GET_TIME(ANDROID_ALARM_RTC);
    if( ioctl(android_alarm_fd, get_time, &now) != 0 ) {
	dsme_log(LOG_ERR, PFIX"%s: %m", "ANDROID_ALARM_TIME_GET");
	goto cleanup;
    }

    struct timespec wup = now;
    wup.tv_sec += delay;

    int set_alarm = ANDROID_ALARM_SET(ANDROID_ALARM_RTC_WAKEUP);
    if( ioctl(android_alarm_fd, set_alarm, &wup) != 0 ) {
	dsme_log(LOG_ERR, PFIX"%s: %m", "ANDROID_ALARM_SET");
	goto cleanup;
    }

    if( android_alarm_prev != wup.tv_sec ) {
	android_alarm_prev = wup.tv_sec;
	dsme_log(LOG_INFO, PFIX"android: %s", t_repr(now.tv_sec, tmp, sizeof tmp));
	dsme_log(LOG_INFO, PFIX"alarm  : %s", t_repr(wup.tv_sec, tmp, sizeof tmp));
    }

cleanup:
    return;
}

/** Remove rtc wakeup via android alarm device
 */
static void android_alarm_clear(void)
{
    if( android_alarm_fd == -1 )
	goto cleanup;

    int cmd = ANDROID_ALARM_CLEAR(ANDROID_ALARM_RTC_WAKEUP);

    if( ioctl(android_alarm_fd, cmd) != 0 ) {
	dsme_log(LOG_ERR, PFIX"%s: %m", "ANDROID_ALARM_CLEAR");
	goto cleanup;
    }

    if( android_alarm_prev != -1 ) {
	android_alarm_prev = -1;
	dsme_log(LOG_INFO, PFIX"android alarm wakeup removed");
    }

cleanup:
    return;
}

/* ------------------------------------------------------------------------- *
 * circumventing read only RTC time of day
 * ------------------------------------------------------------------------- */

static time_t rtc_get_time_tm(struct tm *tm);

/** Flag for: RTC_SET_TIME does not work
 *
 * If true, rtc time is not used for updating the system time.
 *
 * Probed after rtc device is opened.
 */
static bool deltatime_is_needed = false;

/** Counter for: update delta value on rtc interrupt */
static int deltatime_updates_todo = 0;

/** Delta between rtc and system time
 *
 * Access only via deltatime_get/set/update() functions
 */
static time_t deltatime_cached = 0;

/** Persistent storage path for deltatime_cached */
#define DELTATIME_CACHE_FILE "/var/tmp/delta-time"

/** Read and cache delta value stored in DELTATIME_CACHE_FILE
 */
static time_t deltatime_get(void)
{
    static bool load_attempted = false;

    int fd = -1;

    if( load_attempted )
        goto cleanup;

    load_attempted = true;

    if( (fd = open(DELTATIME_CACHE_FILE, O_RDONLY)) == -1 ) {
        if( errno != ENOENT )
            dsme_log(LOG_ERR, PFIX"%s: %s: %m", DELTATIME_CACHE_FILE, "open");
        goto cleanup;
    }

    char tmp[32];
    int len = read(fd, tmp, sizeof tmp - 1);
    if( len < 0 ) {
        dsme_log(LOG_ERR, PFIX"%s: %s: %m", DELTATIME_CACHE_FILE, "read");
        goto cleanup;
    }

    tmp[len] = 0;

    deltatime_cached = strtol(tmp, 0, 0);

    dsme_log(LOG_WARNING, PFIX"rtc delta is %ld", (long)deltatime_cached);

cleanup:

    if( fd != -1 ) close(fd);

    return deltatime_cached;
}

/** Store delta value in to DELTATIME_CACHE_FILE
 */
static void deltatime_set(time_t delta)
{
    int fd = -1;

    deltatime_cached = delta;

    dsme_log(LOG_WARNING, PFIX"rtc delta to %ld", (long)deltatime_cached);

    fd = open(DELTATIME_CACHE_FILE, O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if( fd == -1 ) {
        dsme_log(LOG_ERR, PFIX"%s: %s: %m", DELTATIME_CACHE_FILE, "open");
        goto cleanup;
    }

    char tmp[32];
    int len = snprintf(tmp, sizeof tmp, "%ld\n", (long)delta);

    if( len > 0 && write(fd, tmp, len) == -1 ) {
        dsme_log(LOG_ERR, PFIX"%s: %s: %m", DELTATIME_CACHE_FILE, "write");
        goto cleanup;
    }

cleanup:

    if( fd != -1 ) close(fd);
}

/** Re-calculate delta value and update DELTATIME_CACHE_FILE if changed
 */
static void deltatime_update(void)
{
    struct tm tm;

    /* Not applicaple if the rtc device is not open */
    if( rtc_fd == -1 )
        goto cleanup;

    time_t delta = 0;

    if( deltatime_is_needed ) {
        /* Calculate system time - rtc time delta */
        time_t t_rtc = rtc_get_time_tm(&tm);
        time_t t_sys = time(0);
        delta = t_sys - t_rtc;
    }

    /* Treat off-by-couple-of-seconds as non-error */
    if( abs(delta) <= 2 )
        delta = 0;

    /* We expect to see some jitter in the calculated delta values.
     * Ignore minor values to avoid excessive filesystem updates. */
    if( abs(deltatime_get() - delta) > 2 )
        deltatime_set(delta);

cleanup:

    return;
}

/* ------------------------------------------------------------------------- *
 * RTC management functionality
 * ------------------------------------------------------------------------- */

/** Human readable representation of struct tm
 */
static char *tm_repr(const struct tm *tm, char *buff, size_t size)
{
    snprintf(buff, size, "%04d-%02d-%02d %02d:%02d:%02d %s",
	     tm->tm_year  + 1900,
	     tm->tm_mon   + 1,
	     tm->tm_mday  + 0,
	     tm->tm_hour,
	     tm->tm_min,
	     tm->tm_sec,
	     tm->tm_zone ?: "???");
    return buff;
}

/** Human readable representation of time_t
 */
static char *t_repr(time_t t, char *buff, size_t size)
{
    struct tm tm;
    return tm_repr(gmtime_r(&t, &tm), buff, size);
}

/** Helper for logging rtc time values via dsme_log()
 *
 * @param lev syslog logging priority
 * @param tod rtc time data
 * @param msg string to add before timestamp
 */
static void rtc_log_time(int lev, const char *msg, const struct rtc_time *tod)
{
    dsme_log(lev, "%s%04d-%02d-%02d %02d:%02d:%02d",
	     msg ?: "", tod->tm_year + 1900, tod->tm_mon + 1, tod->tm_mday,
	     tod->tm_hour, tod->tm_min, tod->tm_sec);
}

/** Convert struct tm to struct rtc_time
 *
 * @param tod where to store rtc time
 * @param tm  broken down time
 *
 * @return tm as seconds from start of epoch, or -1 if tm is invalid
 */
static time_t rtc_time_from_tm(struct rtc_time *tod, const struct tm *tm)
{
    /* normalize broken down time */
    struct tm tmp = *tm;
    time_t res = timegm(&tmp);

    /* copy values */
    memset(tod, 0, sizeof *tod);
    tod->tm_sec  = tmp.tm_sec;
    tod->tm_min  = tmp.tm_min;
    tod->tm_hour = tmp.tm_hour;
    tod->tm_mday = tmp.tm_mday;
    tod->tm_mon  = tmp.tm_mon;
    tod->tm_year = tmp.tm_year;

    return res;
}

/** Convert struct rtc_time to struct tm
 *
 * @param tod rtc time
 * @param tm  where to store broken down time
 *
 * @return tod as seconds from start of epoch, or -1 if tod is invalid
 */
static time_t rtc_time_to_tm(const struct rtc_time *tod, struct tm *tm)
{
    /* copy values */
    memset(tm, 0, sizeof *tm);
    tm->tm_sec  = tod->tm_sec;
    tm->tm_min  = tod->tm_min;
    tm->tm_hour = tod->tm_hour;
    tm->tm_mday = tod->tm_mday;
    tm->tm_mon  = tod->tm_mon;
    tm->tm_year = tod->tm_year;

    /* normalize broken down time */
    time_t res = timegm(tm);

    return res;
}

/** Get rtc time as struct rtc_time
 *
 * @param tod where to store broken down rtc time
 *
 * @return true on success, or false in case of errors
 */
static bool rtc_get_time_raw(struct rtc_time *tod)
{
    bool result = false;

    if( rtc_fd == -1 )
	goto cleanup;

    memset(tod, 0, sizeof *tod);

    if( ioctl(rtc_fd, RTC_RD_TIME, tod) == -1 ) {
	dsme_log(LOG_ERR, PFIX"%s: %s: %m", rtc_path, "RTC_RD_TIME");
	goto cleanup;
    }

    rtc_log_time(LOG_DEBUG, PFIX"rtc time is: ", tod);

    result = true;

cleanup:

    return result;
}

/** Get rtc time as struct tm
 *
 * @param tm  where to store broken down time, or NULL
 *
 * @return seconds since epoch, or (time_t)-1 in case of errors
 */
static time_t rtc_get_time_tm(struct tm *tm)
{
    time_t result = -1;

    struct rtc_time tod;

    if( !rtc_get_time_raw(&tod) )
	goto cleanup;

    result = rtc_time_to_tm(&tod, tm);

cleanup:

    return result;
}

/** Set rtc time from struct rtc_time
 *
 * @param tod broken down rtc time
 *
 * @return true on success, or false in case of errors
 */
static bool rtc_set_time_raw(struct rtc_time *tod)
{
    bool result = false;

    if( rtc_fd == -1 )
	goto cleanup;

    if( ioctl(rtc_fd, RTC_SET_TIME, tod) == -1 ) {
	dsme_log(LOG_ERR, PFIX"%s: %s: %m", rtc_path, "RTC_SET_TIME");
	goto cleanup;
    }

    rtc_log_time(LOG_INFO, PFIX"set rtc time to: ", tod);

    result = true;

cleanup:

    return result;
}

/** Set rtc time from struct tm
 *
 * @param tm broken down time
 *
 * @return true on success, or false in case of errors
 */
static bool rtc_set_time_tm(struct tm *tm)
{
    bool result = false;

    struct rtc_time tod;

    memset(&tod, 0, sizeof tod);
    if( rtc_time_from_tm(&tod, tm) < 0 )
	goto cleanup;

    if( !rtc_set_time_raw(&tod) )
	goto cleanup;

    result = true;

cleanup:

    return result;
}

/** Set rtc time from time_t
 *
 * @param t seconds since epoch
 *
 * @return true on success, or false in case of errors
 */
static bool rtc_set_time_t(time_t t)
{
    bool result = false;

    struct tm tm;

    memset(&tm, 0, sizeof tm);
    if( gmtime_r(&t, &tm) == 0 )
	goto cleanup;

    if( !rtc_set_time_tm(&tm) )
	goto cleanup;

    result = true;

cleanup:

    return result;
}

static bool rtc_set_time_tv(const struct timeval *rtc)
{
    bool result = false;

    if( !rtc_set_time_t(rtc->tv_sec) )
	goto cleanup;

    result = true;

cleanup:

    return result;
}

static bool rtc_get_time_tv(struct timeval *rtc)
{
    bool result = false;

    struct tm tm;
    time_t t;

    if( (t = rtc_get_time_tm(&tm)) == -1 )
	goto cleanup;

    rtc->tv_sec  = t;
    rtc->tv_usec = 0;
    result = true;

cleanup:

    return result;
}

/** Set rtc alarm from struct rtc_time
 *
 * @param tod broken down rtc time
 *
 * @return true on success, or false in case of errors
 */
static bool rtc_set_alarm_raw(const struct rtc_time *tod, bool enabled)
{
    static struct rtc_wkalrm prev = { .enabled = 0xff };

    bool result = false;

    struct rtc_wkalrm alrm;

    if( rtc_fd == -1 )
	goto cleanup;

    memset(&alrm, 0, sizeof alrm);
    alrm.enabled = enabled;
    alrm.pending = 0;
    alrm.time    = *tod;

    if( !memcmp(&prev, &alrm, sizeof prev) ) {
	result = true;
	goto cleanup;
    }

    if( enabled )
	rtc_log_time(LOG_INFO, PFIX"set rtc wakeup alarm at ", tod);
    else if( prev.enabled )
	dsme_log(LOG_INFO, PFIX"disable rtc wakeup alarm");

    if( ioctl(rtc_fd, RTC_WKALM_SET, &alrm) == -1 ) {
	dsme_log(LOG_ERR, PFIX"%s: %s: %m", rtc_path, "RTC_WKALM_SET");
	goto cleanup;
    }

    prev = alrm;
    result = true;

cleanup:

    return result;
}

/** Set rtc alarm from struct tm
 *
 * @param tm broken down time
 *
 * @return true on success, or false in case of errors
 */
static bool rtc_set_alarm_tm(const struct tm *tm, bool enabled)
{
    bool result = false;

    struct rtc_time tod;
    time_t t;

    memset(&tod, 0, sizeof tod);
    if( (t = rtc_time_from_tm(&tod, tm)) < 0 )
	goto cleanup;

    if( !rtc_set_alarm_raw(&tod, enabled) )
	goto cleanup;

    result = true;

cleanup:

    return result;
}

/** Program wakeup alarm to /dev/rtc after specified delay
 *
 * @param delay seconds from now to alarm time
 *
 * @return true on success, or false in case of errors
 */
static bool rtc_set_alarm_after(time_t delay)
{
    bool result = false;
    bool enabled = false;

    time_t sys = time(0);
    time_t alm = sys + delay;

    struct tm tm;
    char tmp[32];

    dsme_log(LOG_INFO, PFIX"wakeup delay %d", (int)delay);
    dsme_log(LOG_INFO, PFIX"system : %s", t_repr(sys, tmp, sizeof tmp));
    dsme_log(LOG_INFO, PFIX"alarm  : %s", t_repr(alm, tmp, sizeof tmp));

    if( rtc_fd == -1 )
	goto cleanup;

    if( rtc_get_time_tm(&tm) == (time_t)-1 )
	goto cleanup;

    if( delay > 0 ) {
	tm.tm_sec += delay;
	enabled = true;
    }

    if( enabled )
	android_alarm_set(delay);
    else
	android_alarm_clear();

    if( !rtc_set_alarm_tm(&tm, enabled) )
	goto cleanup;

    result = true;

cleanup:

    return result;
}

/** Set rtc wakeup to happen at the next power up alarm time
 *
 * To be called at module unload time so that wakeup alarm
 * is left to the state timed wants it to be.
 */
static void rtc_set_alarm_powerup(void)
{
    time_t sys = time(0);
    time_t rtc = alarm_powerup;

    /* how far in the future the next poweup alarm is */
    if( rtc > sys )
	rtc -= sys;
    else
	rtc = 0;

    /* adjust down by estimate of boot time to act dead mode */
    rtc -= STARTUP_TIME_ESTIMATE_SECS;

    /* do not program alarms that we cant serve */
    if( rtc < SHUTDOWN_TIME_ESTIMATE_SECS )
	rtc = 0;

    /* always log the state we leave rtc wakeup on dsme exit */
    log_time_t(LOG_CRIT, PFIX"powerup via RTC", rtc ? sys+rtc : 0, sys);

    rtc_set_alarm_after(rtc);
}

/** Flag for: update system time on rtc interrupt */
static bool rtc_to_system_time = false;

/** Handle input from /dev/rtc
 *
 * @return true on success, or false in case of errors
 */
static bool rtc_handle_input(void)
{
    bool result = false;
    long status = 0;

    wakelock_lock(rtc_input, -1);

    dsme_log(LOG_INFO, PFIX"wakeup via RTC alarm");

    if( rtc_fd == -1 ) {
	dsme_log(LOG_WARNING, PFIX"failed to read %s: %s",  rtc_path,
		"the device node is not opened");
	goto cleanup;
    }

    /* clear errno so that we do not report stale "errors"
     * on succesful but partial reads */
    errno = 0;

    if( read(rtc_fd, &status, sizeof status) != sizeof status ) {
	dsme_log(LOG_WARNING, PFIX"failed to read %s: %m",  rtc_path);
	goto cleanup;
    }

    /* rtc time - which has 1 second accuracy - just changed -> update
     * higher resolution system time with assumption that any fraction
     * over full seconds is close to zero at this point */
    if( rtc_to_system_time ) {
	rtc_to_system_time = false;

	struct timeval tv;

	if( deltatime_is_needed )
	    dsme_log(LOG_WARNING, PFIX"rtc not writable; not using it as system time source");
	else if( !rtc_get_time_tv(&tv) )
	    dsme_log(LOG_WARNING, PFIX"failed to read rtc time");
	else if( settimeofday(&tv, 0) == -1 )
	    dsme_log(LOG_WARNING, PFIX"failed to set system time");
	else
	    dsme_log(LOG_INFO, PFIX"system time set from rtc");

	/* Keep the update interrupts active for few more rounds to
	 * make sure we get stable rtc delta statistics */
	deltatime_updates_todo = 5;
    }

    /* Update sys_time vs rtc_time delta statistics a few times, then
     * disable the rtc update interrupts */
    if( deltatime_updates_todo > 0 ) {
	deltatime_update();

	if( --deltatime_updates_todo == 0 ) {
	    if( ioctl(rtc_fd, RTC_UIE_OFF, 0) == -1 )
		dsme_log(LOG_WARNING, PFIX"failed to disable update interrupts");
	}
    }

    /* acquire wakelock that is passed to mce via ipc */
    wakelock_lock(rtc_wakeup, -1);

    result = true;

cleanup:

    wakelock_unlock(rtc_input);

    return result;
}

/** Remove rtc fd from epoll set and close the file descriptor
 */
static void rtc_detach(void)
{
    if( rtc_fd != -1 ) {
	epollfd_remove_fd(rtc_fd);
	close(rtc_fd), rtc_fd = -1;

	dsme_log(LOG_INFO, PFIX"closed %s", rtc_path);
    }
}

/** Open rtc and and add the file descriptor to the epoll set
 *
 * @return true on success, or false in case of errors
 */
static bool rtc_attach(void)
{
    int fd = -1;

    if( rtc_fd != -1 )
	goto cleanup;

    if( (fd = open(rtc_path, O_RDONLY)) == -1 ) {
	dsme_log(LOG_WARNING, PFIX"failed to open %s: %m", rtc_path);
	goto cleanup;
    }

    if( !epollfd_add_fd(fd, &rtc_fd)) {
	dsme_log(LOG_WARNING, PFIX"failed to add rtc fd to epoll set");
	goto cleanup;
    }

    /* N.B. rtc_xxx utilities can be called after rtc_fd is set */
    rtc_fd = fd, fd = -1;
    dsme_log(LOG_INFO, PFIX"opened %s", rtc_path);

    /* deal with obviously wrong rtc time values */
    systemtime_init();

cleanup:

    if( fd != -1 ) close(fd);

    return rtc_fd != -1;
}

/* ------------------------------------------------------------------------- *
 * kernelfd
 * ------------------------------------------------------------------------- */

/** Handle iphb event from kernel side
 */
static void kernelfd_handle_event(void)
{
    /* tell the driver that we have dealt with the event */
    while (read(kernelfd, 0, 0) == -1 && errno == EINTR);
}

/** Open kernel module handle if not already open
 *
 * Can retry later if fails (meaning LKM is not loaded)
 */
static void kernelfd_open(void)
{
    static const char msg[] = HB_LKM_KICK_ME_PERIOD;

    static bool kernel_module_load_error_logged = false;

    int saved_errno;

    if( kernelfd != -1 )
	return;

    if( (kernelfd = open(HB_KERNEL_DEVICE, O_RDWR)) != -1 )
	goto initialize;

    saved_errno = errno; /* log with errno from HB_KERNEL_DEVICE */

    if( (kernelfd = open(HB_KERNEL_DEVICE_TEST, O_RDWR)) != -1 )
	goto initialize;

    if( !kernel_module_load_error_logged ) {
	kernel_module_load_error_logged = true;
	errno = saved_errno;
	if( errno == ENOENT ) {
	    /* Having a kernel that supports iphb wakeups is a special case.
	     *
	     * Not having it just means we are missing one wakeup source to
	     * synchronize with i.e. no need to create noise about it on
	     * default verbosity level */
	    dsme_log(LOG_INFO, PFIX"kernel does not support iphb wakeups");
	}
	else
	    dsme_log(LOG_ERR, PFIX"failed to open kernel connection '%s' (%m)",
		     HB_KERNEL_DEVICE);
    }

    return;

initialize:
    dsme_log(LOG_DEBUG, PFIX"opened kernel socket %d to %s, "
	     "wakeup from kernel=%s",
	     kernelfd,
	     HB_KERNEL_DEVICE,
	     msg);

    if( write(kernelfd, msg, sizeof msg) == -1 ) {
	dsme_log(LOG_ERR, PFIX"failed to write kernel message (%m)");
	// TODO: do something clever?
    }
    else if( !epollfd_add_fd(kernelfd, &kernelfd) ) {
	dsme_log(LOG_ERR, PFIX"failed to add kernel fd to epoll set");
	// TODO: do something clever?
    }
}

/** Close kernel module handle */
static void kernelfd_close(void)
{
    if( kernelfd != -1 ) {
	epollfd_remove_fd(kernelfd);
        close(kernelfd);
        dsme_log(LOG_DEBUG, PFIX"closed kernel socket %d", kernelfd);
        kernelfd = -1;
    }
}

/* ------------------------------------------------------------------------- *
 * libiphb clients
 * ------------------------------------------------------------------------- */

/** Create a new external client instance
 *
 * @param fd	Socket descriptor
 *
 * @return pointer to client object
 */
static client_t *client_new_external(int fd)
{
    client_t *self = calloc(1, sizeof *self);

    if( !self )
	abort();

    self->fd = fd;

    /* Have something valid as description. Overrides are
     * in client_new_internal() and client_handle_wait_req() */
    self->pidtxt = strdup("unknown");

    /* All external clients should wake up from suspend too */
    self->wakeup = true;

    return self;
}

/** Create a new internal client instance
 *
 * @param conn endpoint to wake up when triggered
 * @param data message data to be send on trigger
 *
 * @return pointer to client object
 */
static client_t *client_new_internal(endpoint_t *conn, void* data)
{
    static unsigned id = 0;
    client_t *self = client_new_external(-1);

    self->conn   = endpoint_copy(conn);
    self->data   = data;

    free(self->pidtxt);
    if( asprintf(&self->pidtxt,"internal-%u", ++id) < 0 )
	self->pidtxt = strdup("error");

    /* By default internal clients do not wake up from suspend */
    self->wakeup = false;

    return self;
}

/** Test if client has started a wait period
 *
 * @param self pointer to client object
 *
 * @return true if wait period is active, false otherwise
 */
static bool client_wait_started(const client_t *self)
{
    return timerisset(&self->reqtime);
}

/** Test if client is external
 *
 * @param self pointer to client object
 *
 * @return true if client is external, false otherwise
 */
static bool client_is_external(const client_t *self)
{
    return self->fd != -1;
}

/** Test if client needs resume
 *
 * @param self pointer to client object
 *
 * @return true if client needs resume, false otherwise
 */
static bool client_needs_resume(const client_t *self)
{
    return self->wakeup;
}

/** Wake up a client
 *
 * @param self pointer to client object
 * @param now  current monotonic time
 *
 * @return true if client was woken, or false in case of errors
 */
static bool client_wakeup(client_t *self, const struct timeval *now)
{
    bool woken_up = false;

    struct timeval tv;

    timersub(now, &self->reqtime, &tv);

    dsme_log(LOG_DEBUG, PFIX"waking up client %s who has slept %ld secs",
	     self->pidtxt, (long)tv.tv_sec);

    if( client_is_external(self) ) {
        struct _iphb_wait_resp_t resp = { 0 };

        resp.waited = tv.tv_sec;

        if( send(self->fd, &resp, sizeof resp, MSG_DONTWAIT|MSG_NOSIGNAL) == sizeof resp)
            woken_up = true;
    }
    else {
        DSM_MSGTYPE_WAKEUP msg = DSME_MSG_INIT(DSM_MSGTYPE_WAKEUP);

        msg.resp.waited = tv.tv_sec;
        msg.data        = self->data;

        endpoint_send(self->conn, &msg);
        woken_up = true;
    }

    timerclear(&self->reqtime);

    return woken_up;
}

/** Delete a client instance
 *
 * Release all dynamically allocated resources associated with
 * the client object.
 *
 * @note The client must be already removed from the clientlist
 *       with clientlist_remove_client().
 *
 * @param self pointer to client object
 */
static void client_close_and_free(client_t *self)
{
    free(self->pidtxt);

    if (client_is_external(self)) {
	epollfd_remove_fd(self->fd);
        close(self->fd);
    }
    else {
        endpoint_free(self->conn);
    }

    free(self);
}

/** Adjust periodic wake up time
 *
 * Make sure the period is
 * - multiple of PERIOD_SLOTSIZE seconds
 * - but at least PERIOD_MINIMUM seconds
 *
 * @param period wakeup period [seconds] requested by the client
 *
 * @return adjusted wakeup period [seconds]
 */
static int client_adjust_period(int period)
{
    enum {
	PERIOD_SLOTSIZE = 30,
	PERIOD_MINIMUM  = PERIOD_SLOTSIZE * 1,
    };

    /* round to multiple of PERIOD_SLOTSIZE */
    period += (PERIOD_SLOTSIZE + 1) / 2;
    period -= (period % PERIOD_SLOTSIZE);

    /* not less than PERIOD_MINIMUM */
    if( period < PERIOD_MINIMUM )
	period = PERIOD_MINIMUM;

    return period;
}

/** Adjust minimum value of wakeup range
 *
 * Make sure the minimum wakeup value is not too short in
 * comparison to the maximum value.
 *
 * For example requesting a range of [60s, 24h] does not make
 * too much sense from scheduling point of view and is thus changed
 * to [23h, 24h].
 *
 * @param mintime requested minimum wakeup delay [seconds]
 * @param mintime requested maximum wakeup delay [seconds]
 *
 * @return adjusted minimum wakeup delay [seconds]
 */
static int client_adjust_mintime(int mintime, int maxtime)
{
    enum { S = 1, M = 60*S, H = 60*M, D=24*H };

    static const struct {
	int maxtime;
	int maxdiff;
    } lut[] = {
	{ 24*H, 60*M }, // if maxtime >= 24h, then mintime >= maxtime - 1h
	{ 12*H, 30*M },
	{  6*H,  9*M },
	{ 90*M,  3*M },
	{ 30*M,  1*M },
	{ 10*M, 30*S },
	{  3*M, 20*S },
	{  1*M, 15*S }, // if maxtime >= 60s, then mintime >= maxtime - 15s
	{ 10*S,  5*S }, // if maxtime >= 10s, then mintime >= maxtime - 5s
	{  1*S,  0*S }, // if maxtime >=  1s, then mintime == maxtime
	{ 0 , 0}
    };

    for( int i = 0; lut[i].maxtime; ++i ) {
	if( maxtime < lut[i].maxtime )
	    continue;
	if( mintime + lut[i].maxdiff < maxtime )
	    mintime = maxtime - lut[i].maxdiff;
	break;
    }

    if( mintime > maxtime )
	mintime = maxtime;

    return mintime;
}

/** Set/cancel wait period for the client
 *
 * @param self pointer to client object
 * @param req  wait request data
 * @param now  current monotonic time
 *
 * @return true if client canceled wait, false otherwise
 */
static bool client_handle_wait_req(client_t                      *self,
				   const struct _iphb_wait_req_t *req,
				   const struct timeval          *now)
{
    bool client_woken = false;

    int req_mintime = req->mintime;
    int req_maxtime = req->maxtime;

    if( req->version >= 1 ) {
	/* Time range is expanded to 32 bits */
	req_mintime |= (req->mintime_hi << 16);
	req_maxtime |= (req->maxtime_hi << 16);

	/* Also external clients can choose not to resume */
	self->wakeup = req->wakeup;
    }

    int mintime = req_mintime;
    int maxtime = req_maxtime;

    if( self->pid != req->pid ) {
	free(self->pidtxt);
	self->pidtxt = pid2text(req->pid);
    }

    /* reset mintime & maxtime to time-of-request */
    self->reqtime = self->mintime = self->maxtime = *now;

    if( mintime == 0 && maxtime == 0 ) {
	/* connect/cancel */
        if (!self->pid) {
            dsme_log(LOG_DEBUG, PFIX"client %s connected", self->pidtxt);
        }
	else {
            dsme_log(LOG_DEBUG, PFIX"client %s canceled wait", self->pidtxt);
            client_woken = true;
        }
	/* mark as not-started */
	timerclear(&self->reqtime);
    }
    else if( mintime == maxtime ) {
	/* wakeup in aligned slot */
	mintime = client_adjust_period(mintime);

	if( mintime != req_mintime )
	    dsme_log(LOG_DEBUG, PFIX"client %s adjusted slot: %d -> %d",
		     self->pidtxt, req_mintime, mintime);

	dsme_log(LOG_DEBUG, PFIX"client %s wakeup at %d slot",
		 self->pidtxt, mintime);

	mintime = maxtime = mintime - (mintime + now->tv_sec) % mintime;

	/* slots triggering happens at full seconds */
	self->mintime.tv_usec = 0;
	self->maxtime.tv_usec = 0;
    }
    else  {
	/* wakeup in [min, max] range */
	mintime = client_adjust_mintime(mintime, maxtime);

	if( mintime != req_mintime )
	    dsme_log(LOG_DEBUG, PFIX"client %s adjusted mintime: %d -> %d",
		     self->pidtxt, req_mintime, mintime);

	dsme_log(LOG_DEBUG, PFIX"client %s wakeup at %d-%d range",
		 self->pidtxt, mintime, maxtime);
    }

    /* adjust wakeup range by filtered mintime & maxtime */
    self->mintime.tv_sec += mintime;
    self->maxtime.tv_sec += maxtime;

    self->pid = req->pid;

    /* Update wakeup from susped flag from internal request */
    if( !client_is_external(self) )
	self->wakeup = (req->wakeup != 0);

    if( self->wakeup )
	dsme_log(LOG_DEBUG, PFIX"client %s wakeup flag set", self->pidtxt);

    return client_woken;
}

/** Send status report to external client
 *
 * The status data includes
 * - number of clients
 * - number of clients with active wakeup request
 * - seconds to the next client to be woken up
 *
 * @param self pointer to client object
 */
static void client_handle_stat_req(client_t *self)
{
    struct iphb_stats stats   = { 0 };
    int               next_hb = INT_MAX;
    int               flags   = MSG_DONTWAIT | MSG_NOSIGNAL;

    struct timeval    tv_now;

    monotime_get_tv(&tv_now);

    for( client_t *c = clients; c; c = c->next ) {
        stats.clients++;

	if( !client_wait_started(c) )
	    continue;

	stats.waiting++;

	if( next_hb > c->maxtime.tv_sec )
	    next_hb = c->maxtime.tv_sec;
    }

    if( next_hb < INT_MAX )
	stats.next_hb = next_hb - tv_now.tv_sec;

    if( send(self->fd, &stats, sizeof stats, flags) != sizeof stats )
    {
        dsme_log(LOG_ERR, PFIX"failed to send to client %s (%m)",
		 self->pidtxt);
	// do not drop yet
    }
}

/* ------------------------------------------------------------------------- *
 * list of IPHB clients
 * ------------------------------------------------------------------------- */

/** Find internal client based on endpoint and data to be sent
 *
 * @param conn endpoint to wake up when triggered
 * @param data message data to be send on trigger
 *
 * @return pointer to client object, or NULL if not found
 */
static client_t *clientlist_find_internal_client(endpoint_t *conn, void* data)
{
    for( client_t *client = clients; client; client = client->next ) {
        if( client_is_external(client) )
	    continue;

	if( client->data == data && endpoint_same(client->conn, conn) )
	    return client;
    }
    return 0;
}

/** Add client instance to list of clients
 *
 * @param newclient client instance to add
 */
static void clientlist_add_client(client_t *newclient)
{
    client_t *client;

    if( (client = clients) != 0 ) {
	/* add to end */
	while (client->next)
	    client = client->next;
	client->next = newclient;
    }
    else {
	/* first one */
	clients = newclient;
    }
}

/** Remove client instance from list of clients
 *
 * @param client client instance to remove
 */
static void clientlist_remove_client(client_t *client)
{
    for( client_t **pos = &clients; *pos; pos = &(*pos)->next ) {
	if( *pos == client ) {
	    *pos = client->next;
	    client->next = 0;
	    break;
	}
    }
}

/** Remove client instance from list of clients and then delete it
 *
 * @param client client instance to remove and delete
 */
static void clientlist_delete_client(client_t *client)
{
    /* remove the client from the list */
    clientlist_remove_client(client);

    /* release dynamic resources */
    client_close_and_free(client);
}

/** Delete all clients included in the list of clients
 */
static void clientlist_delete_clients(void)
{
    client_t *client;

    while( (client = clients) != 0 ) {
	/* detach head from list*/
	clients = client->next;
	client->next = 0;

	/* release dynamic resources */
	client_close_and_free(client);
    }
}

/** Calculate seconds to the next alarm
 *
 * Based on time stamps cached from signals sent by timed
 * calculate the seconds left to the next normal / powerup
 * alarm.
 *
 * @note Timed sends timestamps in system time, thus it must
 *       be used here too instead of the monotonic clock.
 *
 * @return seconds to next timed alarm, or 0 in case of no alarms
 */
static time_t clientlist_get_alarm_time(void)
{
    time_t sys = time(0);
    time_t res = INT_MAX;

    if( alarm_powerup > sys && alarm_powerup < res )
	res = alarm_powerup;

    if( alarm_resume > sys && alarm_resume < res )
	res = alarm_resume;

    if( res > sys && res < INT_MAX )
	res -= sys;
    else
	res = 0;

    return res;
}

/** "infinity" value for struct timeval data */
static const struct timeval tv_invalid = { INT_MAX, 0 };

/** Reprogram the rtc wakeup alarm
 *
 * Calculate the time when the next client needs to be woken up.
 *
 * Adjust down if there are alarms before that.
 *
 * Synchronize rtc clock with system time and enable/disable
 * the rtc wakeup alarm.
 */
static void clientlist_rethink_rtc_wakeup(const struct timeval *now)
{
    /* start with no wakeup */
    struct timeval wakeup = tv_invalid;
    time_t         sleeptime = INT_MAX;
    time_t         alarmtime = 0;

    /* scan closest external client wakeup time */
    for( client_t *client = clients; client; client = client->next ) {
	if( !client_needs_resume(client) )
	    continue;

	if( !client_wait_started(client) )
	    continue;

	if( tv_gt(&client->maxtime, now) && tv_gt(&wakeup, &client->maxtime) )
	    wakeup = client->maxtime;
    }

    /* convert from monotonic time stamp to delay */
    if( tv_gt(&wakeup, now) && tv_lt(&wakeup, &tv_invalid) )
	sleeptime = wakeup.tv_sec - now->tv_sec;

    /* check time to next timed alarm, adjust delay if sooner */
    alarmtime = clientlist_get_alarm_time();
    if( alarmtime > 0 && sleeptime > alarmtime )
	sleeptime = alarmtime;

    /* Even if there are not clients, we want rtc wakeup every
     * now and then to drive the battery monitoring during suspend */
#if RTC_MAXIMUM_WAKEUP_TIME
    if( sleeptime < 0 || sleeptime > RTC_MAXIMUM_WAKEUP_TIME ) {
	dsme_log(LOG_DEBUG, PFIX"truncating sleep: %ld -> %ld seconds",
		 (long)sleeptime, (long)RTC_MAXIMUM_WAKEUP_TIME);
	sleeptime = RTC_MAXIMUM_WAKEUP_TIME;
    }
#endif

    /* program rtc wakeup alarm (or disable it) */
    if( sleeptime < 0 || sleeptime >= INT_MAX )
	sleeptime = 0;

    rtc_set_alarm_after(sleeptime);

    deltatime_update();
}

/** Timer callback function for waking up clients between heartbeats
 *
 * @param userdata (not used)
 *
 * @return FALSE (to stop the timer from repeating)
 */
static gboolean clientlist_handle_wakeup_timeout(gpointer userdata)
{
    (void)userdata;

    /* Already canceled? */
    if( !wakeup_timer )
	return FALSE;

    wakeup_timer = 0;

    dsme_log(LOG_DEBUG, PFIX"wakeup via normal timer");

    struct timeval   tv_now;
    monotime_get_tv(&tv_now);

    clientlist_wakeup_clients_now(&tv_now);

    return FALSE; /* stop the interval */
}

/** Start timer for waking up clients before the next heartbeat
 *
 * @param sleep_time timeout delay
 */
static void clientlist_start_wakeup_timeout(const struct timeval *sleep_time)
{
    /* already have a timer? */
    if( wakeup_timer )
	return;

    int ms = sleep_time->tv_sec * 1000 + sleep_time->tv_usec / 1000;

    dsme_log(LOG_DEBUG, PFIX"setting a wakeup in %d ms", ms);
    wakeup_timer = g_timeout_add(ms, clientlist_handle_wakeup_timeout, 0);
}

/** Cancel timer for waking up clients before the next heartbeat
 */
static void clientlist_cancel_wakeup_timeout(void)
{
    if( wakeup_timer )
	g_source_remove(wakeup_timer), wakeup_timer = 0;
}

/** Helper for formatting time-to values for logging purposes
 *
 * @param tv   timeval, from timersub(target_time, current_time, time_diff)
 * @param buff buffer to format time remaining string to
 * @param size size of the output buffer
 */
static char *time_minus(const struct timeval *tv, char *buff, size_t size)
{
    char *pos = buff;
    char *end = buff + size - 1;
    char  tmp[32];

    auto void add(const char *str)
    {
	while( pos < end && *str ) *pos++ = *str++;
    }

    const char *n = "T-";
    long s  = tv->tv_sec;
    long ms = tv->tv_usec / 1000;

    if( s < 0 ) {
	n = "T+";
	// N.B. -0.7s as timeval: tv_sec = -1, tv_usec = 300000
	s = -(s + 1);
	ms = 1000 - ms;
    }

    long m = s / 60; s %= 60;
    long h = m / 60; m %= 60;
    long d = h / 24; h %= 24;

    add(n);
    if( d )
    {
	snprintf(tmp, sizeof tmp, "%ld days, ", d);
	add(tmp);
    }
    snprintf(tmp, sizeof tmp, "%02ld:%02ld:%02ld.%03ld", h, m, s, ms);
    add(tmp);
    return *pos = 0, buff;
}


/** Wakeup clients if conditions are met
 *
 * We should arrive here if:
 * - new clients connect via libiphb
 * - old clients make requests via libiphb
 * - we get iphb events from kernelfd
 * - we get heartbeat message (from hwwd kicker)
 * - intra heartbeat timer triggers (clients with short min-max range)
 */
static void clientlist_wakeup_clients_now(const struct timeval *now)
{
    struct timeval sleep_time = { INT_MAX, 0 };

    int externals_left = 0;

    struct timeval tv_to_max;
    struct timeval tv_to_min;
    char stamp[64];

    dsme_log(LOG_DEBUG, PFIX"check if clients need waking up");
    clientlist_wakeup_clients_cancel();
    clientlist_cancel_wakeup_timeout();

    bool must_wake = false;

    /* 1st pass: are there external clients that we *must* wake up */
    for( client_t *client = clients; client; client = client->next ) {
        if( !client_wait_started(client) )
	    continue;

	if( !client_needs_resume(client) )
	    continue;

	if( tv_lt(now, &client->mintime) )
	    continue;

	timersub(&client->maxtime, now, &tv_to_max);
	if( tv_to_max.tv_sec >= DSME_HEARTBEAT_INTERVAL )
	    continue;

	/* mintime passed and maxtime is less than heartbeat away */
	dsme_log(LOG_DEBUG, PFIX"client %s must be woken up", client->pidtxt);
	must_wake = true;
	break;
    }

    /* 2nd pass: actually wake up clients */
    for( client_t *client = clients, *next; client; client = next ) {
	/* get next before possible clientlist_delete_client() call */
	next = client->next;

        if( !client_wait_started(client) ) {
            dsme_log(LOG_DEBUG, PFIX"client %s not scheduled", client->pidtxt);
	    continue;
        }

	timersub(&client->maxtime, now, &tv_to_max);

	if( tv_lt(now, &client->mintime) ) {
	    timersub(&client->mintime, now, &tv_to_min);
	    dsme_log(LOG_DEBUG, PFIX"client %s min wakeup %s",
		     client->pidtxt,
		     time_minus(&tv_to_min, stamp, sizeof stamp));

	    /* mintime not reached yet */
	    if( tv_to_max.tv_sec >= DSME_HEARTBEAT_INTERVAL || !client_needs_resume(client) ) {
		/* timer is not used for internal clients */
	    }
	    else {
		/* we need timer if maxtime is before the next heartbeat */
		if( tv_gt(&sleep_time, &tv_to_max) )
		    sleep_time = tv_to_max;
	    }
	}
	else if( !must_wake && tv_to_max.tv_sec >= DSME_HEARTBEAT_INTERVAL ) {
	    /* maxtime is at least one heartbeat away */
	    dsme_log(LOG_DEBUG, PFIX"client %s max wakeup %s",
		     client->pidtxt,
		     time_minus(&tv_to_max, stamp, sizeof stamp));
	}
	else {
	    /* due now, wakeup */
	    if( !client_wakeup(client, now) ) {
		dsme_log(LOG_ERR, PFIX"failed to send to client %s (%m),"
			 " drop client", client->pidtxt);
		clientlist_delete_client(client), client = 0;
	    }
	    continue;
	}

	/* count active, but untriggered external clients */
	if( client_is_external(client) )
	    externals_left += 1;
    }

    if( sleep_time.tv_sec < INT_MAX ) {
	clientlist_start_wakeup_timeout(&sleep_time);
    }

    /* open or close the kernel fd as needed */
    if( !externals_left )
        kernelfd_close();
    else
        kernelfd_open();

    /* reprogram the rtc wakeup */
    clientlist_rethink_rtc_wakeup(now);

    /* and tell hwwd kicker we are alive */
    hwwd_feeder_sync();
}


/** Timer id for delayed wakeup checking
 *
 * Note: A wakelock must be held while waiting for timeout.
 *       Otherwise the RTC wakeup might not get programmed
 *       directly due to clients issuing wakeup requests.
 */
static guint clientlist_wakeup_clients_id = 0;

/** Timer callback delayed wakeup checking
 *
 * @param aptr (not used)
 *
 * @return FALSE, to stop timer from repeating
 */
static gboolean clientlist_wakeup_clients_cb(gpointer aptr)
{
    (void)aptr;

    struct timeval   tv_now;

    if( !clientlist_wakeup_clients_id )
	goto EXIT;

    /* Mark timer has handled, this must be done prior to
     * making the clientlist_wakeup_clients_now() call.
     * Otherwise the wakelock might get released too early. */
    clientlist_wakeup_clients_id = 0;

    /* Handle client wakeups */
    monotime_get_tv(&tv_now);
    clientlist_wakeup_clients_now(&tv_now);

    /* If the timer has not been reprogrammed, release wakelock */
    if( !clientlist_wakeup_clients_id )
	wakelock_unlock(iphb_wakeup);

EXIT:
    return FALSE;
}

/** Cancel delayed wakeup checking
 */
static void clientlist_wakeup_clients_cancel(void)
{
    if( clientlist_wakeup_clients_id ) {
	dsme_log(LOG_DEBUG, PFIX"cancel delayed wakeup checking");
	g_source_remove(clientlist_wakeup_clients_id),
	    clientlist_wakeup_clients_id = 0;
	wakelock_unlock(iphb_wakeup);
    }
}

/** Schedule delayed wakeup checking
 *
 * This function starts a wakelock backed timer for delayed client
 * wakeup check up. The purpose is to avoid going through the client
 * list multiple times when
 * a) a client issues several wakeup request
 * b) several clients set new wakeups after synchronous wakeup
 *
 * Additionally it makes (a) or (b) happening comprehensible in the logs
 *
 * The delay must be long enough to be effective, but short enough
 * not to cause wakeup skippage - for now 200 ms is used.
 *
 * @param now monotime stamp (not used at the moment)
 */
static void clientlist_wakeup_clients_later(const struct timeval *now)
{
    (void)now;

    if( !clientlist_wakeup_clients_id ) {
	dsme_log(LOG_DEBUG, PFIX"schedule delayed wakeup checking");
	wakelock_lock(iphb_wakeup, -1);
	clientlist_wakeup_clients_id =
	    g_timeout_add(200, clientlist_wakeup_clients_cb, 0);
    }
}
/* ------------------------------------------------------------------------- *
 * IPC with TIMED
 * ------------------------------------------------------------------------- */

/** Handler for timed next_bootup_event signal
 *
 * The signal has two time_t parameters as INT32 data.
 *
 * The first is for wake up from power off / suspend kind of alarms
 * (in practice clock alarms), and the seconds one for wake up from
 * suspend kind of alarms.
 *
 * The latter is not supported by older timed versions.
 *
 * Both are used for inserting rtc wakeup alarms while dsme is running,
 * and the first one is left active in rtc when dsme exits.
 */
static void xtimed_alarm_status_cb(const DsmeDbusMessage* ind)
{
    time_t new_powerup = dsme_dbus_message_get_int(ind);

    /* NOTE: Does not matter in this case, but ... Old timed
     *       versions broadcast only the powerup time and
     *       dsme_dbus_message_get_int() function returns zero
     *       if the dbus message does not have INT32 type data
     *       to parse. */
    time_t new_resume  = dsme_dbus_message_get_int(ind);
    time_t sys = time(0);

    dsme_log(LOG_NOTICE, PFIX"alarm state from timed: powerup=%ld, resume=%ld",
	     (long)new_powerup, (long)new_resume);

    if( new_powerup < sys || new_powerup >= INT_MAX )
	new_powerup = 0;

    if( new_resume < sys || new_resume >= INT_MAX )
	new_resume = 0;

    if( alarm_powerup != new_powerup || alarm_resume  != new_resume ) {
	alarm_powerup = new_powerup;
	alarm_resume  = new_resume;
	struct timeval now;
	monotime_get_tv(&now);
	clientlist_rethink_rtc_wakeup(&now);
    }

    log_time_t(LOG_INFO, "powerup time", alarm_powerup, sys);
    log_time_t(LOG_INFO, "resume  time", alarm_resume,  sys);
}

/** Handler for timed settings_changed signal
 *
 * In theory we should parse the signal content and check if the
 * system time changed flag is set, but as clientlist_rethink_rtc_wakeup()
 * checks system time vs rtc time diff this is not necessary.
 *
 * @param ind dbus signal (not used)
 */
static void xtimed_config_status_cb(const DsmeDbusMessage* ind)
{
    dsme_log(LOG_INFO, PFIX"settings change from timed");

    /* rethink will sync rtc time with system time */
    struct timeval now;
    monotime_get_tv(&now);
    clientlist_rethink_rtc_wakeup(&now);
}

/** Store alarm queue data to a file
 */
static void xtimed_status_save(void)
{
  const char *path = XTIMED_STATE_FILE;
  const char *temp = XTIMED_STATE_FILE".tmp";

  FILE *file = 0;
  int rc;

  if( remove(temp) == -1 && errno != ENOENT ) {
    dsme_log(LOG_ERR, PFIX"%s: %s: %m", temp, "remove");
    goto cleanup;
  }

  if( !(file = fopen(temp, "w")) ) {
    dsme_log(LOG_ERR, PFIX"%s: %s: %m", temp, "open");
    goto cleanup;
  }

  rc = fprintf(file, "%ld %ld\n", (long)alarm_powerup, (long)alarm_resume);
  if( rc < 0 ) {
    dsme_log(LOG_ERR, PFIX"%s: %s: %m", temp, "write");
    goto cleanup;
  }

  if( fflush(file) == EOF ) {
    dsme_log(LOG_ERR, PFIX"%s: %s: %m", temp, "flush");
    goto cleanup;
  }

  rc = fclose(file), file = 0;
  if( rc == EOF ) {
    dsme_log(LOG_ERR, PFIX"%s: %s: %m", temp, "close");
    goto cleanup;
  }

  if( rename(temp, path) == -1 ) {
    dsme_log(LOG_ERR, PFIX"%s: rename to %s: %m", temp, path);
  }

cleanup:
  if( file ) fclose(file);
}

/** Restore alarm queue data from a file
 */
static void xtimed_status_load(void)
{
  const char *path = XTIMED_STATE_FILE;

  FILE *file = 0;

  if( !(file = fopen(path, "r")) ) {
      if( errno != ENOENT )
	  dsme_log(LOG_ERR, PFIX"%s: %s: %m", path, "open");
    goto cleanup;
  }

  long powerup = 0, resume = 0;

  if( fscanf(file, "%ld %ld", &powerup, &resume) != 2 ) {
    dsme_log(LOG_ERR, PFIX"%s: %s: did not get two values", path, "read");
    goto cleanup;
  }
  alarm_powerup = (time_t)powerup;
  alarm_resume  = (time_t)resume;

cleanup:
  if( file ) fclose(file);
}

/* ------------------------------------------------------------------------- *
 * listenfd
 * ------------------------------------------------------------------------- */

/** Handle new client connecting via libiphb
 */
static void listenfd_handle_connect(void)
{
    int newfd = accept(listenfd, 0, 0);

    if( newfd == -1 ) {
	dsme_log(LOG_ERR, PFIX"failed to accept client (%m)");
    }
    else {
	client_t *client = client_new_external(newfd);
	if (epollfd_add_fd(newfd, client)) {
	    clientlist_add_client(client);
	    dsme_log(LOG_DEBUG, PFIX"new client added to list");
	}
	else {
	    clientlist_delete_client(client);
	}
    }
}

/** Stop accepting new libiphb clients
 */
static void listenfd_quit(void)
{
    if( listenfd != -1 ) {
	epollfd_remove_fd(listenfd);
	close(listenfd);
	listenfd = -1;
    }

    if( unlink(HB_SOCKET_PATH) == -1 && errno != ENOENT ) {
        dsme_log(LOG_WARNING, PFIX"failed to remove client listen socket %s: %m",
		 HB_SOCKET_PATH);
    }
}

/** Start accepting new libiphb clients
 *
 * @return true on success, or false on failure
 */
static bool listenfd_init(void)
{
    bool result = false;
    mode_t mode = S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH;

    struct sockaddr_un addr;

    if( unlink(HB_SOCKET_PATH) == -1 && errno != ENOENT ) {
        dsme_log(LOG_WARNING, PFIX"failed to remove client listen socket %s: %m",
		 HB_SOCKET_PATH);
	// try to continue anyway
    }

    if( (listenfd = socket(PF_UNIX, SOCK_STREAM, 0)) < 0 ) {
        dsme_log(LOG_ERR, PFIX"failed to open client listen socket: %m");
        goto cleanup;
    }

    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, HB_SOCKET_PATH);

    if( bind(listenfd, (struct sockaddr *) &addr, sizeof(addr)) == -1 ) {
        dsme_log(LOG_ERR, PFIX"failed to bind client listen socket to %s: %m",
                 HB_SOCKET_PATH);
        goto cleanup;
    }

    if( chmod(HB_SOCKET_PATH, mode) == -1 ) {
        dsme_log(LOG_ERR, PFIX"failed to chmod %o '%s': %m", (int)mode, HB_SOCKET_PATH);
        goto cleanup;
    }

    if( listen(listenfd, 5) == -1 ) {
        dsme_log(LOG_ERR, PFIX"failed to listen client socket: %m");
        goto cleanup;
    }

    dsme_log(LOG_DEBUG, PFIX"opened client socket %d to %s",
	     listenfd,
	     HB_SOCKET_PATH);

    // add the listening socket to the epoll set
    if (!epollfd_add_fd(listenfd, &listenfd)) {
        goto cleanup;
    }

    result = true;

cleanup:

    // all or nothing
    if( !result )
	listenfd_quit();

    return result;
}

/* ------------------------------------------------------------------------- *
 * epollfd
 * ------------------------------------------------------------------------- */

/** Add filedescriptor to the epoll set
 *
 * @param fd   file descriptor to add
 * @param ptr  data to associate with the file descriptor
 *
 * @return true on success, or false on failure
 */
static bool epollfd_add_fd(int fd, void* ptr)
{
    struct epoll_event ev = { 0, { 0 } };
    ev.events   = EPOLLIN;
    ev.data.ptr = ptr;

    if( epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev) == -1 ) {
	dsme_log(LOG_ERR, PFIX"failed to add fd=%d to epoll set: %m", fd);
	return false;
    }

    return true;
}

/** Remove filedescriptor from the epoll set
 *
 * @param fd   file descriptor to remove
 */
static void epollfd_remove_fd(int fd)
{
    if( epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0) == -1 ) {
	dsme_log(LOG_ERR, PFIX"failed to remove fd=%d from epoll set: %m", fd);
    }
}

/** Handle epoll event associated with libiphb client
 *
 * @param event epoll event to handle
 * @param now   current monotonic time
 *
 * @return true if client canceled wait, false otherwise
 */
static bool epollfd_handle_client_req(struct epoll_event* event,
				      const struct timeval *now)
{
    bool      client_woken = false;
    client_t *client       = event->data.ptr;

    if( event->events & (EPOLLERR | EPOLLRDHUP | EPOLLHUP) ) {
        dsme_log(LOG_DEBUG, PFIX"client %s disappeared",
                 client->pidtxt);
        goto drop_client_and_fail;
    }

    struct _iphb_req_t req = { 0 };

    if( recv(client->fd, &req, sizeof req, MSG_WAITALL) <= 0 ) {
        dsme_log(LOG_ERR, PFIX"failed to read from client %s: %m",
                 client->pidtxt);
        goto drop_client_and_fail;
    }

    switch (req.cmd) {
        case IPHB_WAIT:
            client_woken = client_handle_wait_req(client, &req.u.wait, now);
            break;

        case IPHB_STAT:
            client_handle_stat_req(client);
            break;

        default:
            dsme_log(LOG_ERR, PFIX"client %s gave invalid command 0x%x, drop it",
                     client->pidtxt,
                     (unsigned int)req.cmd);
            goto drop_client_and_fail;
    }

    return client_woken;

drop_client_and_fail:
    clientlist_delete_client(client);
    return false;
}

/** I/O watch callback for the epoll set
 *
 * The epoll set handles
 * - new clients connecting via libiphb
 * - old clients making requests via libiphb
 * - rtc wakeup alarms from /dev/rtc
 * - iphb events from kernel
 *
 * @param source     glib io channel associated with epollfd
 * @param condition  (unused)
 * @param data       (unused)
 *
 * @return TRUE to keep the iowatch alive, or FALSE to stop it on errors
 */
static gboolean epollfd_iowatch_cb(GIOChannel*  source,
				   GIOCondition condition,
				   gpointer     data)
{
    bool               wakeup_mce = false;

    struct timeval     tv_now;
    struct epoll_event events[DSME_MAX_EPOLL_EVENTS];
    int                nfds;

    /* Abandon watch if we get abnorman conditions from glib */
    if (condition & ~(G_IO_IN | G_IO_PRI))
    {
	dsme_log(LOG_ERR, PFIX"epoll waiting I/O error reported");
	dsme_log(LOG_CRIT, PFIX"epoll waiting disabled");
	return FALSE;
    }

    nfds = epoll_wait(epollfd, events, DSME_MAX_EPOLL_EVENTS, 0);

    if( nfds == -1 ) {
	if( errno == EINTR || errno == EAGAIN )
	    return TRUE;

        dsme_log(LOG_ERR, PFIX"epoll waiting failed (%m)");
        dsme_log(LOG_CRIT, PFIX"epoll waiting disabled");
	return FALSE;
    }

    monotime_get_tv(&tv_now);

    /* go through new events */
    for( int i = 0; i < nfds; ++i ) {
        if (events[i].data.ptr == &listenfd) {
            /* accept new clients */
	    listenfd_handle_connect();
        }
	else if (events[i].data.ptr == &kernelfd) {
	    /* iphb event from kernel */
	    kernelfd_handle_event();
        }
	else if (events[i].data.ptr == &rtc_fd) {
	    /* rtc wakeup (and possibly resume from suspend) */
	    if( !(wakeup_mce = rtc_handle_input()) )
		rtc_detach();
        }
	else {
            /* deal with old clients */
            epollfd_handle_client_req(&events[i], &tv_now);
        }
    }

    clientlist_wakeup_clients_later(&tv_now);

    if( wakeup_mce ) {
	/* Allow mce some time to take over the wakeup, but ... */
	wakelock_lock(rtc_wakeup, RTC_WAKEUP_TIMEOUT_MS);

	/* .. unlock immediately if we can't do ipc with mce */
	if( !xmce_cpu_keepalive_wakeup() )
	    wakelock_unlock(rtc_wakeup);
    }

    /* We might not get rtc on startup, try again if needed */
    if( rtc_fd == -1 )
	rtc_attach();

    return TRUE;
}

/** Stop the epoll io watch */
static void epollfd_quit(void)
{
    if( epoll_watch )
	g_source_remove(epoll_watch), epoll_watch = 0;

    if( epollfd != -1 )
	close(epollfd), epollfd = -1;

}

/** Start the epoll io watch
 *
 * @return true on success, or false on failure
 */
static bool epollfd_init(void)
{
    bool        result = false;
    GIOChannel *chan = 0;

    if( (epollfd = epoll_create(10)) == -1 ) {
        dsme_log(LOG_ERR, PFIX"failed to open epoll fd (%m)");
	goto cleanup;
    }

    if (!(chan = g_io_channel_unix_new(epollfd))) {
        goto cleanup;
    }

    if( !(epoll_watch = g_io_add_watch(chan,
				       G_IO_IN|G_IO_ERR|G_IO_HUP|G_IO_NVAL,
				       epollfd_iowatch_cb, 0)) ) {
	goto cleanup;
    }

    result = true;

cleanup:

    if( chan )
	g_io_channel_unref(chan);

    return result;
}

/* ------------------------------------------------------------------------- *
 * D-Bus systembus connection management (for IPC with MCE)
 * ------------------------------------------------------------------------- */

/** Get a system bus connection not bound by dsme_dbus abstractions
 *
 * To be called when D-Bus is available notification is received.
 */
static void systembus_connect(void)
{
    DBusError err = DBUS_ERROR_INIT;

    if( !(systembus = dsme_dbus_get_connection(&err)) ) {
	dsme_log(LOG_WARNING, PFIX"can't connect to systembus: %s: %s",
		 err.name, err.message);
	goto cleanup;
    }

    dbus_connection_setup_with_g_main(systembus, 0);

    xmce_handle_dbus_connect();

cleanup:

    dbus_error_free(&err);
}

/** Detach from systembus connection obtained via systembus_connect()
 *
 * To be called at module unload / when D-Bus no longer available
 * notification is received.
 */
static void systembus_disconnect(void)
{
    if( systembus ) {
	xmce_handle_dbus_disconnect();
	dbus_connection_unref(systembus), systembus = 0;
    }
}

/* ------------------------------------------------------------------------- *
 * D-Bus signal handler callbacks
 * ------------------------------------------------------------------------- */

/** Signal binding state, set to true if signal handlers are installed */
static bool bound = false;

/** Array of signal handlers to install when D-Bus connection is available */
static const dsme_dbus_signal_binding_t signals[] =
{
    { xtimed_alarm_status_cb,  "com.nokia.time", "next_bootup_event" },
    { xtimed_config_status_cb, "com.nokia.time", "settings_changed" },
    { 0, 0, 0 }
};

/* ------------------------------------------------------------------------- *
 * Handlers for internal messages
 * ------------------------------------------------------------------------- */

/** Handle heartbeat from hwwd kicking activity */
DSME_HANDLER(DSM_MSGTYPE_HEARTBEAT, conn, msg)
{
    struct timeval   tv_now;

    dsme_log(LOG_DEBUG, PFIX"HEARTBEAT from HWWD");
    monotime_get_tv(&tv_now);
    clientlist_wakeup_clients_now(&tv_now);
}

/** Handle WAIT requests from internal clients */
DSME_HANDLER(DSM_MSGTYPE_WAIT, conn, msg)
{
    dsme_log(LOG_DEBUG, PFIX"WAIT req from an internal client");

    struct timeval   tv_now;
    monotime_get_tv(&tv_now);

    client_t *client = clientlist_find_internal_client(conn, msg->data);
    if (!client) {
        client = client_new_internal(conn, msg->data);
        clientlist_add_client(client);
    }

    client_handle_wait_req(client, &msg->req, &tv_now);

    if( client_needs_resume(client) ) {
	/* Internal requests with wakeup flag set are handled similarly
	 * to external requests */
	clientlist_wakeup_clients_later(&tv_now);
    }
    else {
	/* We don't want to wake anyone else up for internal clients showing up.
	 * And internal clients do not use timers or rtc interrupts.
	 * -> skip wakeup scanning and just feed the hwhw kicker */
	hwwd_feeder_sync();
    }

}

/** Handle connected to system bus */
DSME_HANDLER(DSM_MSGTYPE_DBUS_CONNECT, client, msg)
{
    dsme_log(LOG_INFO, PFIX"DBUS_CONNECT");
    dsme_dbus_bind_signals(&bound, signals);
    systembus_connect();
}

/** Handle disconnected from system bus */
DSME_HANDLER(DSM_MSGTYPE_DBUS_DISCONNECT, client, msg)
{
    dsme_log(LOG_INFO, PFIX"DBUS_DISCONNECT");
    dsme_dbus_unbind_signals(&bound, signals);
    systembus_disconnect();
}

module_fn_info_t message_handlers[] =
{
    DSME_HANDLER_BINDING(DSM_MSGTYPE_HEARTBEAT),
    DSME_HANDLER_BINDING(DSM_MSGTYPE_WAIT),

    DSME_HANDLER_BINDING(DSM_MSGTYPE_DBUS_CONNECT),
    DSME_HANDLER_BINDING(DSM_MSGTYPE_DBUS_DISCONNECT),
    { 0 }
};

/* ------------------------------------------------------------------------- *
 * System time setup / update
 * ------------------------------------------------------------------------- */

static time_t get_mtime(const char *path)
{
    struct stat st;

    if( stat(path, &st) == 0 )
	return st.st_mtime;

    if( errno != ENOENT )
	dsme_log(LOG_ERR, PFIX"%s: failed to get mtime: %m", path);

    return 0;
}

static time_t mintime_fetch(void)
{
    struct tm tm =
    {
	// 2013-12-01 12:00:00 UTC
	.tm_sec   = 0,
	.tm_min   = 0,
	.tm_hour  = 12,

	.tm_mday  = 10   - 0,
	.tm_mon   = 12   - 1,
	.tm_year  = 2013 - 1900,

	.tm_wday  = -1,
	.tm_yday  = -1,
	.tm_isdst = -1,
    };

    time_t    t_builtin = timegm(&tm);
    time_t    t_release = get_mtime(IMAGE_TIME_STAMP_FILE);
    time_t    t_saved   = get_mtime(SAVED_TIME_FILE);
    time_t    t_system  = time(0);
    char      tmp[32];

    dsme_log(LOG_INFO, PFIX"builtin %s", t_repr(t_builtin, tmp, sizeof tmp));
    dsme_log(LOG_INFO, PFIX"release %s", t_repr(t_release, tmp, sizeof tmp));
    dsme_log(LOG_INFO, PFIX"saved   %s", t_repr(t_saved,   tmp, sizeof tmp));
    dsme_log(LOG_INFO, PFIX"system  %s", t_repr(t_system,  tmp, sizeof tmp));

    if( t_saved < t_builtin )
	t_saved = t_builtin;

    if( t_saved < t_release )
	t_saved = t_release;

    if( t_saved < t_system )
	t_saved = t_system;

    return t_saved;
}

static void mintime_store(void)
{
    static const char *path =  SAVED_TIME_FILE;
    int fd = open(path, O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if( fd == -1 )
	dsme_log(LOG_ERR, PFIX"%s: failed to open for writing: %m", path);
    else
	close(fd);
}

static void systemtime_init(void)
{
    char tmp[32];
    struct tm tm;

    /* Get current state */

    time_t t_sys = time(0);
    time_t t_min = mintime_fetch();
    time_t t_rtc = rtc_get_time_tm(&tm);

    dsme_log(LOG_NOTICE, PFIX"min at %s", t_repr(t_min, tmp, sizeof tmp));
    dsme_log(LOG_NOTICE, PFIX"rtc at %s", t_repr(t_rtc, tmp, sizeof tmp));
    dsme_log(LOG_NOTICE, PFIX"sys at %s", t_repr(t_sys, tmp, sizeof tmp));

    /* Take possible cached sys-time vs rtc-time delta into account */

    time_t delta = deltatime_get();

    if( delta != 0 ) {
	/* If we have cached delta time; before accepting it
	 * check if RTC_SET_TIME actually fails */
        dsme_log(LOG_WARNING, PFIX"rtc to %s", t_repr(t_rtc, tmp, sizeof tmp));
        if( !rtc_set_time_t(t_rtc) )
            deltatime_is_needed = true;
	else
	    delta = 0;
    }

    t_rtc += delta;

    if( t_min < t_rtc )
        t_min = t_rtc;

    /* Adjust rtc time if it is currently below expected minimum */

    if( t_rtc < t_min ) {
        t_rtc = t_min;
        dsme_log(LOG_WARNING, PFIX"rtc to %s", t_repr(t_rtc, tmp, sizeof tmp));
        if( !rtc_set_time_t(t_rtc) )
            deltatime_is_needed = true;
    }

    /* Adjust system time if rtc time can be assumed to be correct or
     * it is ahead of the system time */

    if( delta == 0 || t_sys < t_rtc ) {
        dsme_log(LOG_WARNING, PFIX"sys to %s", t_repr(t_rtc, tmp, sizeof tmp));
        struct timeval tv = { .tv_sec = t_rtc, .tv_usec = 0 };
        if( settimeofday(&tv, 0) == -1 )
            dsme_log(LOG_WARNING, PFIX"failed to set system time");

        /* system time should now be within 1 second from rtc time */
    }

    /* Enable RTC update interrupts; the 1st one is used to bring system
     * time closer in sync with rtc time and few following ones to update
     * sys-time vs rtc-time delta.
     */

    if( ioctl(rtc_fd, RTC_UIE_ON, 0) == -1 ) {
        dsme_log(LOG_WARNING, PFIX"failed to enable update interrupts");
    }
    else {
        rtc_to_system_time = true;
    }

    /* Update cached rtc delta value; this will be repeated when
     * handling rtc interrupts, programming rtc alarms and
     * just before dsme exits */

    deltatime_update();
}

static void systemtime_quit(void)
{
    mintime_store();
    deltatime_update();
}

/* ------------------------------------------------------------------------- *
 * Module loading & unloading
 * ------------------------------------------------------------------------- */

/** Startup */
void module_init(module_t *handle)
{
    bool success = false;

    dsme_log(LOG_INFO, PFIX"iphb.so loaded");

    /* restore alarm queue state */
    xtimed_status_load();

    /* Clear stale wakelocks that we might have left due to dsme crash etc */
    wakelock_unlock(rtc_wakeup);
    wakelock_unlock(rtc_input);

    /* Initialize epoll set before services that need it */
    if( !epollfd_init() )
	goto cleanup;

    /* Create connect socket and add it to epoll set */
    if( !listenfd_init() )
	goto cleanup;

    /* if possible, add rtc fd to the epoll set */
    rtc_attach();

    /* if available, open android alarm device */
    android_alarm_init();

    success = true;

cleanup:

    if( success )
        dsme_log(LOG_INFO, PFIX"iphb started");
    else
	dsme_log(LOG_ERR, PFIX"iphb not started");

    return;
}

/** Shutdown */
void module_fini(void)
{
    /* cancel timers */
    clientlist_cancel_wakeup_timeout();
    clientlist_wakeup_clients_cancel();

    /* detach dbus handlers */
    dsme_dbus_unbind_signals(&bound, signals);

    /* store alarm queue state to a file*/
    xtimed_status_save();

    /* store system time to rtc */
    struct timeval tv_sys, tv_rtc;
    if( !rtc_get_time_tv(&tv_rtc) )
	dsme_log(LOG_ERR, PFIX"could not get rtc time");
    else if( !realtime_get_tv(&tv_sys) )
	dsme_log(LOG_ERR, PFIX"could not get system time");
    else {
	struct timeval t;
	timersub(&tv_sys, &tv_rtc, &t);

	/* Note: due to how timeval works, we get
	 * nonzero t.tv_sec when rtc > sys || sys+1 >= rtc
	 */

	if( !t.tv_sec )
	    dsme_log(LOG_CRIT, PFIX"RTC in sync with system time");
	else if( !rtc_set_time_tv(&tv_sys) )
	    dsme_log(LOG_ERR, PFIX"could not set rtc time");
	else
	    dsme_log(LOG_CRIT, PFIX"RTC updated to system time");
    }

    /* save last-known-system-time */
    systemtime_quit();

    /* set wakeup alarm before closing the rtc */
    rtc_set_alarm_powerup();
    rtc_detach();

    /* close android alarm device */
    android_alarm_quit();

    /* cleanup rest of what is in the epoll set */
    listenfd_quit();
    kernelfd_close();
    clientlist_delete_clients();

    /* remove the epoll set itself */
    epollfd_quit();

    /* disconnect from system bus */
    systembus_disconnect();

    /* Release wakelocks before exiting */
    wakelock_unlock(rtc_wakeup);
    wakelock_unlock(rtc_input);

    dsme_log(LOG_INFO, PFIX"iphb.so unloaded");
}
