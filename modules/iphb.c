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
#include "dsme/modules.h"
#include "dsme/modulebase.h"
#include "dsme/logging.h"
#include "dsme/timers.h"
#include "dsme/dsme-wdd-wd.h"

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

/* ------------------------------------------------------------------------- *
 * Custom types
 * ------------------------------------------------------------------------- */

/**
 * @brief  Allocated structure of one client in the linked client list in iphbd
 */
typedef struct _client_t {
    int               fd;           /*!< IPC (Unix domain) socket or -1 */
    endpoint_t*       conn;         /*!< internal client endpoint (if fd == -1) */
    void*             data;         /*!< internal client cookie (if fd == -1) */
    time_t            wait_started; /*!< 0 if client has not subscribed to wake-up call */
    unsigned short    mintime;      /*!< min time to sleep in secs */
    unsigned short    maxtime;      /*!< max time to sleep in secs */
    pid_t             pid;          /*!< client process ID */
    struct _client_t* next;         /*!< pointer to the next client in the list (NULL if none) */
} client_t;

/** Wakeup clients predicate callback function type  */
typedef bool (condition_func)(client_t* client, time_t now);

/* ------------------------------------------------------------------------- *
 * Function prototypes
 * ------------------------------------------------------------------------- */

static time_t monotime(void);

static bool wakelock_supported(void);
static void wakelock_write(const char *path, const char *data);
static void wakelock_lock(const char *name, int ms);
static void wakelock_unlock(const char *name);

static DBusMessage *xdbus_create_name_owner_req(const char *name);
static gchar       *xdbus_parse_name_owner_rsp(DBusMessage *rsp);

static void xmce_set_runstate(bool running);
static void xmce_verify_name_cb(DBusPendingCall *pending, void *user_data);
static bool xmce_verify_name(void);
static bool xmce_cpu_keepalive_wakeup(void);
static void xmce_handle_dbus_connect(void);
static void xmce_handle_dbus_disconnect(void);
static DBusHandlerResult xmce_dbus_filter_cb(DBusConnection *con, DBusMessage *msg, void *user_data);

static void   rtc_log_time(int lev, const char *msg, const struct rtc_time *tod);
static time_t rtc_time_from_tm(struct rtc_time *tod, const struct tm *tm);
static time_t rtc_time_to_tm(const struct rtc_time *tod, struct tm *tm);
static bool   rtc_get_time_raw(struct rtc_time *tod);
static time_t rtc_get_time_tm(struct tm *tm);
static bool   rtc_set_time_raw(struct rtc_time *tod);
static bool   rtc_set_time_tm(struct tm *tm);
static bool   rtc_set_time_t(time_t t);
static bool   rtc_sync_to_system_time(void);
static bool   rtc_set_alarm_raw(const struct rtc_time *tod, bool enabled);
static bool   rtc_set_alarm_tm(const struct tm *tm, bool enabled);
static void   rtc_detach(void);
static bool   rtc_attach(void);
static bool   rtc_handle_input(void);
static bool   rtc_set_wakeup(time_t delay);
static void   rtc_rethink_wakeup(time_t now);

static time_t xtimed_alarm_time(time_t now);
static void   xtimed_alarm_status_cb(const DsmeDbusMessage *ind);
static void   xtimed_config_status_cb(const DsmeDbusMessage *ind);
static void   xtimed_apply_powerup_alarm_time(void);

static void      open_kernel_fd(void);
static void      close_kernel_fd(void);
static bool      start_service(void);
static client_t *new_client(int fd);
static client_t *new_internal_client(endpoint_t *conn, void *data);
static bool      is_external_client(const client_t *client);
static void      list_add_client(client_t *newclient);
static client_t *list_find_internal_client(endpoint_t *conn, void *data);
static int       external_clients(void);
static void      send_stats(client_t *client);
static bool      epoll_add(int fd, void *ptr);
static gboolean  read_epoll(GIOChannel *source, GIOCondition condition, gpointer data);
static int       handle_wakeup_timeout(void *unused);
static bool      is_timer_needed(int *optimal_sleep_time);
static bool      handle_client_req(struct epoll_event *event, time_t now);
static bool      handle_wait_req(const struct _iphb_wait_req_t *req_const, client_t *client, time_t now);
static void      wakeup_clients_if(condition_func *should_wake_up, time_t now);
static bool      mintime_passed(client_t *client, time_t now);
static bool      maxtime_passed(client_t *client, time_t now);
static int       wakeup_clients_if2(condition_func *should_wake_up, time_t now);
static long long timestamp(void);
static bool      wakeup(client_t *client, time_t now);
static void      delete_clients(void);
static void      delete_client(client_t *client);
static void      remove_client(client_t *client, client_t *prev);
static void      close_and_free_client(client_t *client);
static void      sync_hwwd_feeder(void);
static void      stop_wakeup_timer(void);

static void      systembus_connect(void);
static void      systembus_disconnect(void);

void module_init(module_t *handle);
void module_fini(void);

/* ------------------------------------------------------------------------- *
 * Variables
 * ------------------------------------------------------------------------- */

/** Linked lits of connected clients */
static client_t* clients = NULL;

/** IPC client listen/accept handle */
static int listenfd = -1;

/** Handle to the kernel */
static int kernelfd = -1;

/** Handle to the epoll instance */
static int epollfd  = -1;

/** I/O watch for epollfd */
static guint epoll_watch = 0;

/** TODO: what is this??? */
static dsme_timer_t wakeup_timer = 0;

/** Monotonic timestamp for iphb start time */
static struct timespec ts_started = {0, 0};

/** System bus connection (for IPC with mce) */
static DBusConnection *systembus = 0;

/** Path to RTC device node */
static const char rtc_path[] = "/dev/rtc0";

/** File descriptor for RTC device node */
static int rtc_fd = -1;

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

/* ------------------------------------------------------------------------- *
 * Generic utility functions
 * ------------------------------------------------------------------------- */

/** Get monotonic time stamp
 *
 * Similar to time(), but returns monotonically increasing
 * second count unaffected by system time changes.
 *
 * @return seconds since unspecified reference point in time
 */
static time_t monotime(void)
{
    struct timespec ts_now;
    clock_gettime(CLOCK_MONOTONIC, &ts_now);
    return ts_now.tv_sec;
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
static void wakelock_write(const char *path, const char *data)
{
    int file;

    if( (file = TEMP_FAILURE_RETRY(open(path, O_WRONLY))) == -1 ) {
	dsme_log(LOG_WARNING, PFIX"%s: open: %m", path);
    }
    else {
	int size = strlen(data);
	errno = 0;
	if( TEMP_FAILURE_RETRY(write(file, data, size)) != size ) {
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
	wakelock_write(lock_path, tmp);
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
	wakelock_write(unlock_path, tmp);
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
	dsme_log(LOG_NOTICE, PFIX"mce state -> %s", running ? "started" : "terminated");
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

    if( !dbus_pending_call_set_notify(pc, xmce_verify_name_cb, 0, 0) )
	goto cleanup;

    // pending call should not be cancelled
    pc = 0;

    // success
    res = true;

cleanup:

    if( pc  ) dbus_pending_call_cancel(pc);
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
 * RTC management functionality
 * ------------------------------------------------------------------------- */

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

    rtc_log_time(LOG_DEBUG, PFIX"rtc time read: ", tod);

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

    rtc_log_time(LOG_NOTICE, PFIX"rtc time set to: ", tod);

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

/** Synchronize rtc time to system time
 *
 * Adjust rtc time if disagrees from system time by more
 * than one second.
 *
 * @return true on success, or false in case of errors
 */
static bool rtc_sync_to_system_time(void)
{
    static time_t delta_old = 0;

    bool success = true;

    time_t time_wall = time(0);
    time_t time_mono = monotime();
    time_t delta_new = time_mono - time_wall;

    time_t dd = delta_new - delta_old;

    if( dd < -1 || dd > 1 ) {
	if( rtc_set_time_t(time_wall) )
	    delta_old = delta_new;
	else
	    success = false;
    }

    return success;
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

    if( ioctl(rtc_fd, RTC_WKALM_SET, &alrm) == -1 ) {
	dsme_log(LOG_ERR, PFIX"%s: %s: %m", rtc_path, "RTC_WKALM_SET");
	goto cleanup;
    }

    if( enabled )
	rtc_log_time(LOG_INFO, PFIX"rtc alarm @ ", tod);
    else if( prev.enabled )
	dsme_log(LOG_INFO, PFIX"rtc alarm disabled");

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

    memset(&tod, 0, sizeof tod);
    if( !rtc_time_from_tm(&tod, tm) )
	goto cleanup;

    if( !rtc_set_alarm_raw(&tod, enabled) )
	goto cleanup;

    result = true;

cleanup:

    return result;
}

/** Remove rtc fd from epoll set and close the file descriptor
 */
static void rtc_detach(void)
{
    if( rtc_fd != -1 ) {
        if( epoll_ctl(epollfd, EPOLL_CTL_DEL, rtc_fd, 0) == -1)
	    dsme_log(LOG_WARNING, PFIX"remove rtc fd from epoll set failed");

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
	dsme_log(LOG_WARNING, PFIX"failed to open %s: %s",
		 rtc_path, strerror(errno));
	goto cleanup;
    }

    if( !epoll_add(fd, &rtc_fd)) {
	dsme_log(LOG_WARNING, PFIX"failed to add rtc fd to epoll set");
	goto cleanup;
    }

    /* N.B. rtc_xxx utilities can be called after rtc_fd is set */
    rtc_fd = fd, fd = -1;
    dsme_log(LOG_INFO, PFIX"opened %s", rtc_path);

    /* synchronize rtc time with system time */
    rtc_sync_to_system_time();

cleanup:

    if( fd != -1 ) close(fd);

    return rtc_fd != -1;
}

/** Handle input from /dev/rtc
 *
 * @return true on success, or false in case of errors
 */
static bool rtc_handle_input(void)
{
    bool result = false;
    long status = 0;

    wakelock_lock(rtc_input, -1);

    if( rtc_fd == -1 ) {
	dsme_log(LOG_WARNING, PFIX"failed to read %s: %s",  rtc_path,
		"the device node is not opened");
	goto cleanup;
    }

    // clear errno so that we do not report stale "errors"
    // on succesful but partial reads
    errno = 0;

    if( read(rtc_fd, &status, sizeof status) != sizeof status ) {
	dsme_log(LOG_WARNING, PFIX"failed to read %s: %m",  rtc_path);
	goto cleanup;
    }

    wakelock_lock(rtc_wakeup, -1);

    dsme_log(LOG_INFO, PFIX"read %s: type=0x%02lx, count=%ld", rtc_path,
	     status & 0xff, status >> 8);
    result = true;

cleanup:

    wakelock_unlock(rtc_input);

    return result;
}

/** Program wakeup alarm to /dev/rtc after specified delay
 *
 * @param delay seconds from now to alarm time
 *
 * @return true on success, or false in case of errors
 */
static bool rtc_set_wakeup(time_t delay)
{
    bool result = false;
    bool enabled = false;

    struct tm tm;

    if( rtc_fd == -1 )
	goto cleanup;

    if( rtc_get_time_tm(&tm) == (time_t)-1 )
	goto cleanup;

    if( delay > 0 ) {
	tm.tm_sec += delay;
	enabled = true;
    }

    if( !rtc_set_alarm_tm(&tm, enabled) )
	goto cleanup;

    result = true;

cleanup:

    return result;
}

/** Reprogram the rtc alarm based on wakeup ranges requested by clients
 */
static void rtc_rethink_wakeup(time_t now)
{
    /* start with no alarm */
    time_t wakeup = INT_MAX;

    time_t t;

    /* update to closest external client wakeup time */
    for( client_t *client = clients; client; client = client->next ) {
	if( !is_external_client(client) )
	    continue;

        if( client->wait_started ) {
	    t = client->wait_started + client->maxtime;
	    if( t > now && wakeup > t )
		wakeup = t;
        }
    }

    /* consider alarm time too */
    t = xtimed_alarm_time(now);
    if( t > now && wakeup > t )
	wakeup = t;

    /* synchronize rtc time with system time */
    rtc_sync_to_system_time();

    /* enable/disable wakeup alarm */
    if( wakeup > now && wakeup < INT_MAX )
	wakeup -= now;
    else
	wakeup = 0;
    rtc_set_wakeup(wakeup);
}

/* ------------------------------------------------------------------------- *
 * IPC with TIMED
 * ------------------------------------------------------------------------- */

/** When the next alarm that should powerup/resume the device is due */
static time_t xtimed_next_powerup = 0;

/** When the next alarm that should resume the device is due */
static time_t xtimed_next_resume  = 0;

/** Evaluate the next timed alarm time in relation to monotonic clock
 *
 * @param now current time from monotonic clock source
 *
 * @return seconds to the next timed alarm, or 0 if no alarms
 */
static time_t xtimed_alarm_time(time_t now)
{
    time_t sys = time(0);
    time_t res = INT_MAX;

    if( xtimed_next_powerup > sys && xtimed_next_powerup < res )
	res = xtimed_next_powerup;

    if( xtimed_next_resume > sys && xtimed_next_resume < res )
	res = xtimed_next_resume;

    if( res > sys && res < INT_MAX )
	res = res - sys + now;
    else
	res = 0;

    return res;
}

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
    time_t next_powerup = dsme_dbus_message_get_int(ind);

    /* NOTE: Does not matter in this case, but ... Old timed
     *       versions broadcast only the powerup time and
     *       dsme_dbus_message_get_int() function returns zero
     *       if the dbus message does not have INT32 type data
     *       to parse. */
    time_t next_resume  = dsme_dbus_message_get_int(ind);
    time_t sys = time(0);

    dsme_log(LOG_NOTICE, PFIX"alarm state from timed: powerup=%ld, resume=%ld",
	     (long)next_powerup, (long)next_resume);

    if( next_powerup < sys || next_powerup >= INT_MAX )
	next_powerup = 0;

    if( next_resume < sys || next_resume >= INT_MAX )
	next_resume = 0;

    if( xtimed_next_powerup != next_powerup ||
	xtimed_next_resume  != next_resume ) {
	xtimed_next_powerup = next_powerup;
	xtimed_next_resume  = next_resume;
	rtc_rethink_wakeup(monotime());
    }
}

/** Handler for timed settings_changed signal
 *
 * In theory we should parse the signal content and check if the
 * system time changed flag is set, but as rtc_rethink_wakeup()
 * checks system time vs rtc time diff this is not necessary.
 *
 * @param ind dbus signal (not used)
 */
static void xtimed_config_status_cb(const DsmeDbusMessage* ind)
{
    dsme_log(LOG_NOTICE, PFIX"settings change from timed");

    /* rethink will sync rtc time with system time */
    rtc_rethink_wakeup(monotime());
}

/** Set rtc wakeup to happen at the next power up alarm time
 *
 * To be called at module unload time so that wakeup alarm
 * is left to the state timed wants it to be.
 */
static void xtimed_apply_powerup_alarm_time(void)
{
    time_t sys = time(0);
    time_t rtc = xtimed_next_powerup;

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

    rtc_set_wakeup(rtc);
}

/* ------------------------------------------------------------------------- *
 * IPHB functionality
 * ------------------------------------------------------------------------- */

/**
 * Open kernel module handle - retry later if fails (meaning LKM is not loaded)
 */
static void open_kernel_fd(void)
{
    static bool kernel_module_load_error_logged = false;

    kernelfd = open(HB_KERNEL_DEVICE, O_RDWR, 0644);
    if (kernelfd == -1) {
        kernelfd = open(HB_KERNEL_DEVICE_TEST, O_RDWR, 0644);
    }
    if (kernelfd == -1) {
        if (!kernel_module_load_error_logged) {
            kernel_module_load_error_logged = true;
            dsme_log(LOG_ERR,
                     PFIX"failed to open kernel connection '%s' (%s)",
                     HB_KERNEL_DEVICE,
                     strerror(errno));
        }
    } else {
        const char *msg;

        msg = HB_LKM_KICK_ME_PERIOD;

        dsme_log(LOG_DEBUG,
                 PFIX"opened kernel socket %d to %s, wakeup from kernel=%s",
                 kernelfd,
                 HB_KERNEL_DEVICE,
                 msg);

        if (write(kernelfd, msg, strlen(msg) + 1) == -1) {
            dsme_log(LOG_ERR,
                     PFIX"failed to write kernel message (%s)",
                     strerror(errno));
            // TODO: do something clever?
        } else if (!epoll_add(kernelfd, &kernelfd)) {
            dsme_log(LOG_ERR, PFIX"failed to add kernel fd to epoll set");
            // TODO: do something clever?
        }
    }
}

static void close_kernel_fd(void)
{
    if (kernelfd != -1) {
        if (epoll_ctl(epollfd, EPOLL_CTL_DEL, kernelfd, 0) == -1) {
            dsme_log(LOG_ERR, PFIX"failed to remove kernel fd from epoll set");
            // TODO: do something clever?
        }
        (void)close(kernelfd);
        dsme_log(LOG_DEBUG, PFIX"closed kernel socket %d", kernelfd);
        kernelfd = -1;
    }
}

/**
 * Start up daemon. Does not fail if kernel module is not loaded
 *
 * @todo clean up in error cases
 */
static bool start_service(void)
{
    struct sockaddr_un addr;

    (void)clock_gettime(CLOCK_MONOTONIC, &ts_started);

    listenfd = socket(PF_UNIX, SOCK_STREAM, 0);
    if (listenfd < 0) {
        dsme_log(LOG_ERR,
                 PFIX"failed to open client listen socket (%s)",
                 strerror(errno));
        goto fail;
    }
    unlink(HB_SOCKET_PATH);
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, HB_SOCKET_PATH);
    if (bind(listenfd, (struct sockaddr *) &addr, sizeof(addr))) {
        dsme_log(LOG_ERR,
                 PFIX"failed to bind client listen socket to %s, (%s)",
                 HB_SOCKET_PATH,
                 strerror(errno));
        goto fail;
    }
    if (chmod(HB_SOCKET_PATH, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH) !=
        0)
    {
        dsme_log(LOG_ERR,
                 PFIX"failed to chmod '%s' (%s)",
                 HB_SOCKET_PATH,
                 strerror(errno));
        goto fail;
    }
    if (listen(listenfd, 5) != 0) {
        dsme_log(LOG_ERR, PFIX"failed to listen client socket (%s)", strerror(errno));
        goto fail;
    }
    else {
        dsme_log(LOG_DEBUG,
                 PFIX"opened client socket %d to %s",
                 listenfd,
                 HB_SOCKET_PATH);
    }

    epollfd = epoll_create(10);
    if (epollfd == -1) {
        dsme_log(LOG_ERR, PFIX"failed to open epoll fd (%s)", strerror(errno));
	goto fail;
    }

    // add the listening socket to the epoll set
    if (!epoll_add(listenfd, &listenfd)) {
        goto fail;
    }

    // set up an I/O watch for the epoll set
    GIOChannel* chan = 0;
    if (!(chan = g_io_channel_unix_new(epollfd))) {
        goto fail;
    }
    epoll_watch = g_io_add_watch(chan, G_IO_IN, read_epoll, 0);
    g_io_channel_unref(chan);
    if (!epoll_watch) {
        goto fail;
    }

    // if possible, add rtc fd to the epoll set
    rtc_attach();

    return true;

fail:
    if( epollfd != -1 )
	close(epollfd), epollfd = -1;

    if( listenfd != -1 )
	close(listenfd), listenfd = -1;

    return false;
}

/**
 * Add new client to list.
 *
 * @param fd	Socket descriptor
 *
 * @todo	Is abort OK if malloc fails?
 */
static client_t* new_client(int fd)
{
    client_t* client;

    client = (client_t*)calloc(1, sizeof(client_t));
    if (client == 0) {
        errno = ENOMEM;
        dsme_log(LOG_ERR, PFIX"malloc(new_client) failed");
        abort(); // TODO
    }
    client->fd = fd;

    return client;
}

static client_t* new_internal_client(endpoint_t* conn, void* data)
{
    client_t* client = new_client(-1);
    client->conn = endpoint_copy(conn);
    client->data = data;

    return client;
}

static bool is_external_client(const client_t* client)
{
    return client->fd != -1;
}

static void list_add_client(client_t* newclient)
{
  client_t *client;

  if (NULL == clients) {
      /* first one */
      clients = newclient;
      return;
  } else {
      /* add to end */
      client = clients;
      while (client->next)
          client = client->next;
      client->next = newclient;
  }
}

static client_t* list_find_internal_client(endpoint_t* conn, void* data)
{
    client_t* client = clients;

    while (client) {
        if (!is_external_client(client)       &&
            endpoint_same(client->conn, conn) &&
            client->data == data)
        {
            break;
        }

        client = client->next;
    }

    return client;
}

static int external_clients()
{
    int       count  = 0;
    client_t* client = clients;

    while (client) {
        if (is_external_client(client)) {
            ++count;
        }
        client = client->next;
    }

    return count;
}

static void send_stats(client_t *client)
{
    struct iphb_stats stats   = { 0 };
    client_t*         c       = clients;
    unsigned int      next_hb = 0;
    struct timespec   ts_now;

    (void)clock_gettime(CLOCK_MONOTONIC, &ts_now);

    while (c) {
        stats.clients++;
        if (c->wait_started) {
            stats.waiting++;
        }

        if (c->wait_started) {
            unsigned int wait_time = c->wait_started + c->maxtime - ts_now.tv_sec;
            if (!next_hb) {
                next_hb = wait_time;
            } else {
                if (wait_time < next_hb) {
                    next_hb = wait_time;
                }
            }
        }

        c = c->next;
    }

    stats.next_hb = next_hb;
    if (send(client->fd, &stats, sizeof(stats), MSG_DONTWAIT|MSG_NOSIGNAL) !=
        sizeof(stats))
    {
        char* pidtxt = pid2text(client->pid);
        dsme_log(LOG_ERR,
                 PFIX"failed to send to client with PID %s (%s)",
                 pidtxt,
                 strerror(errno));  // do not drop yet
        free(pidtxt);
    }
}

static bool epoll_add(int fd, void* ptr)
{
  struct epoll_event ev = { 0, { 0 } };
  ev.events   = EPOLLIN;
  ev.data.ptr = ptr;
  if (epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
      dsme_log(LOG_ERR,
               PFIX"failed to add fd %d to epoll set (%s)",
               fd,
               strerror(errno));
      return false;
  }

  return true;
}

static gboolean read_epoll(GIOChannel*  source,
                           GIOCondition condition,
                           gpointer     data)
{
    dsme_log(LOG_DEBUG, PFIX"epollfd readable");

    stop_wakeup_timer();

    struct epoll_event events[DSME_MAX_EPOLL_EVENTS];
    int                nfds;
    int                i;
    condition_func*    wakeup_condition = maxtime_passed;
    bool               wakeup_mce = false;
    while ((nfds = epoll_wait(epollfd, events, DSME_MAX_EPOLL_EVENTS, 0)) == -1
        && errno == EINTR)
    {
        /* EMPTY LOOP */
    }
    if (nfds == -1) {
        dsme_log(LOG_ERR, PFIX"epoll waiting failed (%s)", strerror(errno));
        // TODO: what to do? return false?
    }
    dsme_log(LOG_DEBUG, PFIX"epollfd_wait => %d events", nfds);

    struct timespec   ts_now;

    (void)clock_gettime(CLOCK_MONOTONIC, &ts_now);

    /* go through new events */
    for (i = 0; i < nfds; ++i) {
        if (events[i].data.ptr == &listenfd) {
            /* accept new clients */
            dsme_log(LOG_DEBUG, PFIX"accept() a new client");
            int newfd = accept(listenfd, 0, 0);
            if (newfd != -1) {
                client_t* client = new_client(newfd);
                if (epoll_add(newfd, client)) {
                    list_add_client(client);
                    dsme_log(LOG_DEBUG, PFIX"new client added to list");
                } else {
                    delete_client(client);
                }
            } else {
                dsme_log(LOG_ERR,
                         PFIX"failed to accept client (%s)",
                         strerror(errno));
            }
        } else if (events[i].data.ptr == &kernelfd) {
            wakeup_condition = mintime_passed;
            // tell the driver that we have dealt with the event
            while (read(kernelfd, 0, 0) == -1 && errno == EINTR);
        } else if (events[i].data.ptr == &rtc_fd) {
	    if( !(wakeup_mce = rtc_handle_input()) )
		rtc_detach();
	    wakeup_condition = mintime_passed;
        } else {
            /* deal with old clients */
            if (handle_client_req(&events[i], ts_now.tv_sec)) {
                wakeup_condition = mintime_passed;
            }
        }
    }

    wakeup_clients_if(wakeup_condition, ts_now.tv_sec);

    sync_hwwd_feeder();

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

    // TODO: should we ever stop?
    return TRUE;
}

static int handle_wakeup_timeout(void* unused)
{
    dsme_log(LOG_DEBUG, PFIX"*** TIMEOUT ***");

    struct timespec   ts_now;
    (void)clock_gettime(CLOCK_MONOTONIC, &ts_now);

    wakeup_clients_if(maxtime_passed, ts_now.tv_sec);

    sync_hwwd_feeder();

    return 0; /* stop the interval */
}

static bool is_timer_needed(int* optimal_sleep_time)
{
    bool   client_found = false;

    struct timespec   ts_now;
    (void)clock_gettime(CLOCK_MONOTONIC, &ts_now);

    int    sleep_time   = 0;

    client_t* client = clients;
    while (client) {
        // only set up timers for external clients that are waiting
        if (is_external_client(client) && client->wait_started) {
            // does this client need to wake up before previous ones?
            if (!client_found ||
                client->wait_started + client->maxtime < ts_now.tv_sec + sleep_time)
            {
                client_found = true;
                // make sure to keep sleep_time >= 0
                if (client->wait_started + client->maxtime <= ts_now.tv_sec) {
                    // this client should have been woken up already!
                    sleep_time = 0;
                    break;
                } else {
                    // we have a new shortest sleep time
                    sleep_time = client->wait_started + client->maxtime - ts_now.tv_sec;
                }
            }
        }

        client = client->next;
    }

    if (!client_found || sleep_time >= DSME_HEARTBEAT_INTERVAL) {
        // either no client or we will wake up before the timer anyway
        return false;
    } else {
        // a (short) timer has to be set up to guarantee a wakeup
        *optimal_sleep_time = sleep_time;
        return true;
    }
}

static bool handle_client_req(struct epoll_event* event, time_t now)
{
    client_t* client       = event->data.ptr;
    bool      client_woken = false;

    if (event->events & EPOLLERR ||
        event->events & EPOLLRDHUP ||
        event->events & EPOLLHUP)
    {
        char* pidtxt = pid2text(client->pid);
        dsme_log(LOG_DEBUG,
                 PFIX"client with PID %s disappeared",
                 pidtxt);
        free(pidtxt);
        goto drop_client_and_fail;
    }

    dsme_log(LOG_DEBUG,
             PFIX"client with PID %lu is active",
             (unsigned long)client->pid);

    struct _iphb_req_t req = { 0 };

    if (recv(client->fd, &req, sizeof(req), MSG_WAITALL) <= 0) {
        char* pidtxt = pid2text(client->pid);
        dsme_log(LOG_ERR,
                 PFIX"failed to read from client with PID %s (%s)",
                 pidtxt,
                 strerror(errno));
        free(pidtxt);
        goto drop_client_and_fail;
    }

    char* pidtxt;
    switch (req.cmd) {
        case IPHB_WAIT:
            client_woken = handle_wait_req(&req.u.wait, client, now);
            break;

        case IPHB_STAT:
            send_stats(client);
            break;

        default:
            pidtxt = pid2text(client->pid);
            dsme_log(LOG_ERR,
                     PFIX"client with PID %s gave invalid command 0x%x, drop it",
                     pidtxt,
                     (unsigned int)req.cmd);
            free(pidtxt);
            goto drop_client_and_fail;
    }

    return client_woken;

drop_client_and_fail:
    delete_client(client);
    return false;
}

static bool handle_wait_req(const struct _iphb_wait_req_t* req_const,
                            client_t*                      client,
                            time_t                         now)
{
    bool client_woken = false;
    struct _iphb_wait_req_t req = *req_const;

    if (req.maxtime == 0 && req.mintime == 0) {
        char* pidtxt = pid2text(client->pid);
        if (!client->pid) {
            client->pid = req.pid;
            dsme_log(LOG_DEBUG,
                     PFIX"client with PID %s connected",
                     pidtxt);
        } else {
            dsme_log(LOG_DEBUG,
                     PFIX"client with PID %s canceled wait",
                     pidtxt);
            client_woken = true;
        }
        free(pidtxt);
        client->wait_started = 0;
        client->mintime      = req.mintime;
        client->maxtime      = req.maxtime;
    } else {
        if (req.mintime && req.maxtime == req.mintime) {
            struct timespec ts_now;
            int             slots_passed;
            char* pidtxt = pid2text(req.pid);

            dsme_log(LOG_DEBUG,
                     PFIX"client with PID %s signaled interest of waiting with"
                       " fixed time %d",
                     pidtxt,
                     (int)req.mintime);

            (void)clock_gettime(CLOCK_MONOTONIC, &ts_now);

            slots_passed = (ts_now.tv_sec - ts_started.tv_sec) / req_const->mintime;
            req.mintime = ts_started.tv_sec + (slots_passed + 1) * req_const->mintime - ts_now.tv_sec;
            if (req.mintime <= 1)
                req.mintime = ts_started.tv_sec + (slots_passed + 2) * req_const->mintime - ts_now.tv_sec;
            req.maxtime = req.mintime + 1; /* allow tolerance because of math above */

            dsme_log(LOG_DEBUG,
                     PFIX"fixed reqtimes for client with PID %s"
                       " (min=%d/max=%d)",
                     pidtxt,
                     (int)req.mintime,
                     (int)req.maxtime);
            free(pidtxt);
        } else {
            char* pidtxt = pid2text(req.pid);
            dsme_log(LOG_DEBUG,
                     PFIX"client with PID %s signaled interest of waiting"
                       " (min=%d/max=%d)",
                     pidtxt,
                     (int)req.mintime,
                     (int)req.maxtime);
            free(pidtxt);
        }

        client->pid          = req.pid;
        client->wait_started = now;
        client->mintime      = req.mintime;
        client->maxtime      = req.maxtime;
    }

    return client_woken;
}

static void wakeup_clients_if(condition_func* should_wake_up, time_t now)
{
    // wake up clients in two passes,
    // giving priority to those whose maxtime has passed
    if (wakeup_clients_if2(maxtime_passed, now) ||
        should_wake_up == mintime_passed)
    {
        dsme_log(LOG_DEBUG, PFIX"waking up clients because somebody was woken up");
        wakeup_clients_if2(mintime_passed, now);
    }

    // open or close the kernel fd as needed
    if (!external_clients()) {
        close_kernel_fd();
    } else if (kernelfd == -1) {
        open_kernel_fd();
    }

    /* reprogram the rtc wakeup */
    rtc_rethink_wakeup(now);
}

static bool mintime_passed(client_t* client, time_t now)
{
    return now >= client->wait_started + client->mintime;
}

static bool maxtime_passed(client_t* client, time_t now)
{
    return now >= client->wait_started + client->maxtime;
}

static int wakeup_clients_if2(condition_func* should_wake_up, time_t now)
{
    int woken_up_clients = 0;

    client_t* prev   = 0;
    client_t* client = clients;
    while (client) {
        client_t* next = client->next;
        char* pidtxt = pid2text(client->pid);

        if (!client->wait_started) {
            dsme_log(LOG_DEBUG,
                     PFIX"client with PID %s is active, not to be woken up",
                     pidtxt);
        } else {
            if (should_wake_up(client, now)) {
                if (wakeup(client, now)) {
                    ++woken_up_clients;
                } else {
                    dsme_log(LOG_ERR,
                             PFIX"failed to send to client with PID %s (%s),"
                               " drop client",
                             pidtxt,
                             strerror(errno));
                    remove_client(client, prev);
                    close_and_free_client(client);
                    goto next_client;
                }
            }
        }

        prev = client;
next_client:
        client = next;
        free(pidtxt);
    }

    return woken_up_clients;
}

static long long timestamp(void)
{
    struct timeval tv;

    gettimeofday(&tv, 0);
    return tv.tv_sec * 1000000ll + tv.tv_usec;
}

static bool wakeup(client_t* client, time_t now)
{
    bool woken_up = false;

    if (is_external_client(client)) {
        struct _iphb_wait_resp_t resp = { 0 };
        char* pidtxt = pid2text(client->pid);
        resp.waited = now - client->wait_started;

        dsme_log(LOG_DEBUG,
                 PFIX"waking up client with PID %s who has slept %lu secs"
                     ", ts=%lli",
                 pidtxt,
                 resp.waited,
                 timestamp());
        free(pidtxt);
        if (send(client->fd, &resp, sizeof(resp), MSG_DONTWAIT|MSG_NOSIGNAL) ==
            sizeof(resp))
        {
            woken_up = true;
        }
    } else {
        DSM_MSGTYPE_WAKEUP msg = DSME_MSG_INIT(DSM_MSGTYPE_WAKEUP);
        msg.resp.waited = now - client->wait_started;
        msg.data        = client->data;

        dsme_log(LOG_DEBUG,
                 PFIX"waking up internal client who has slept %lu secs",
                 msg.resp.waited);
        endpoint_send(client->conn, &msg);
        woken_up = true;
    }

    client->wait_started = 0;

    return woken_up;
}

static void delete_clients(void)
{
    while (clients) {
        delete_client(clients);
    }
}

static void delete_client(client_t* client)
{
    /* remove the client from the list */
    client_t* prev = 0;
    client_t* c    = clients;
    while (c) {
        if (client == c) {
            remove_client(client, prev);
            break;
        }

        prev = c;
        c = c->next;
    }

    close_and_free_client(client);
}

static void remove_client(client_t* client, client_t* prev)
{
    if (prev) {
        prev->next = client->next;
    }
    if (client == clients) {
        clients = client->next;
    }
}

static void close_and_free_client(client_t* client)
{
    if (is_external_client(client)) {
        (void)epoll_ctl(epollfd, EPOLL_CTL_DEL, client->fd, 0);
        (void)close(client->fd);
    } else {
        endpoint_free(client->conn);
    }

    free(client);
}

// synchronice to HW WD feeding process by listening to its heartbeat
DSME_HANDLER(DSM_MSGTYPE_HEARTBEAT, conn, msg)
{
    dsme_log(LOG_DEBUG, PFIX"HEARTBEAT from HWWD");

    stop_wakeup_timer();

    struct timespec   ts_now;
    (void)clock_gettime(CLOCK_MONOTONIC, &ts_now);

    // TODO: should we wake up mintime sleepers to sync on hwwd?
    wakeup_clients_if(maxtime_passed, ts_now.tv_sec);
}

static void sync_hwwd_feeder(void)
{
    kill(getppid(), SIGHUP);
}

DSME_HANDLER(DSM_MSGTYPE_WAIT, conn, msg)
{
    dsme_log(LOG_DEBUG, PFIX"WAIT req from an internal client");

    stop_wakeup_timer();

    struct timespec   ts_now;
    (void)clock_gettime(CLOCK_MONOTONIC, &ts_now);

    client_t* client = list_find_internal_client(conn, msg->data);
    if (!client) {
        client = new_internal_client(conn, msg->data);
        list_add_client(client);
    }

    handle_wait_req(&msg->req, client, ts_now.tv_sec);

    /* reprogram the rtc wakeup */
    rtc_rethink_wakeup(ts_now.tv_sec);

    // we don't want to wake anyone else up for internal clients showing up
    // TODO: or do we?

    sync_hwwd_feeder();
}

DSME_HANDLER(DSM_MSGTYPE_IDLE, conn, msg)
{
    // the internal msg queue is empty so we will probably sleep for a while;
    // see if we need to set up a timer to guarantee a timely wakeup
    if (!wakeup_timer) {
        int sleep_time;
        if (is_timer_needed(&sleep_time)) {
            dsme_log(LOG_DEBUG, PFIX"setting a wakeup in %d s", sleep_time);
            wakeup_timer =
                dsme_create_timer_high_priority(sleep_time,
                                                handle_wakeup_timeout,
                                                0);
        }
    }
}

static void stop_wakeup_timer(void)
{
    if (wakeup_timer) {
        dsme_destroy_timer(wakeup_timer);
        wakeup_timer = 0;
    }
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

    if( !(systembus = dbus_bus_get(DBUS_BUS_SYSTEM, &err)) ) {
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

DSME_HANDLER(DSM_MSGTYPE_DBUS_CONNECT, client, msg)
{
    dsme_log(LOG_NOTICE, PFIX"DBUS_CONNECT");
    dsme_dbus_bind_signals(&bound, signals);
    systembus_connect();
}

DSME_HANDLER(DSM_MSGTYPE_DBUS_DISCONNECT, client, msg)
{
    dsme_log(LOG_NOTICE, PFIX"DBUS_DISCONNECT");
    dsme_dbus_unbind_signals(&bound, signals);
    systembus_disconnect();
}

module_fn_info_t message_handlers[] = {
    DSME_HANDLER_BINDING(DSM_MSGTYPE_HEARTBEAT),
    DSME_HANDLER_BINDING(DSM_MSGTYPE_WAIT),
    DSME_HANDLER_BINDING(DSM_MSGTYPE_IDLE),

    DSME_HANDLER_BINDING(DSM_MSGTYPE_DBUS_CONNECT),
    DSME_HANDLER_BINDING(DSM_MSGTYPE_DBUS_DISCONNECT),
    { 0 }
};

/* ------------------------------------------------------------------------- *
 * Module loading & unloading
 * ------------------------------------------------------------------------- */

void module_init(module_t* handle)
{
    dsme_log(LOG_INFO, PFIX"iphb.so loaded");

    /* Clear stale wakelocks that we might have left due to restarting dsme
     * after crash etc. If this happens to be the 1st dsme startup the
     * wakelocks do not exist yet -> lock 1st, then unlock to avoid EINVAL */
    wakelock_lock(rtc_wakeup, -1);
    wakelock_lock(rtc_input, -1);
    wakelock_unlock(rtc_wakeup);
    wakelock_unlock(rtc_input);

    if (!start_service()) {
	dsme_log(LOG_ERR, PFIX"iphb not started");
    }
    else {
        dsme_log(LOG_INFO, PFIX"iphb started");
    }
}

void module_fini(void)
{
    dsme_dbus_unbind_signals(&bound, signals);

    xtimed_apply_powerup_alarm_time();
    rtc_detach();

    delete_clients();

    if( epoll_watch != 0 ) {
	g_source_remove(epoll_watch);
    }

    if( epollfd == -1 ) {
	close(epollfd);
    }

    if (listenfd != -1) {
        close(listenfd);
    }

    if (kernelfd != -1) {
        close(kernelfd);
    }

    systembus_disconnect();

    /* Release wakelocks before exiting */
    wakelock_unlock(rtc_wakeup);
    wakelock_unlock(rtc_input);

    dsme_log(LOG_INFO, PFIX"iphb.so unloaded");
}
