/**
   @file batterytracker.c

   Track the battery charge level. If charge level goes too low, 
   issue warnings. If battery level goes below safe limit, make shutdown

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

#include "dsme/timers.h"
#include "dsme/modules.h"
#include "dsme/logging.h"

#include <dsme/state.h>
#include <dsme/protocol.h>

#include <iphbd/iphb_internal.h>

#include <stdio.h>
#include <limits.h>
#include <time.h>
#include <errno.h>
#include <string.h>

#define BATTERY_LEVEL_PATH         "/run/state/namespaces/Battery/ChargePercentage"
#define CHARGING_INFO_PATH         "/run/state/namespaces/Battery/IsCharging"
#define BATTERY_LEVEL_CONFIG_FILE  "/etc/dsme/battery_levels.conf"

/**
 * Timer value for alarm shutdown timer. This is how long we wait before reporting
 * empty battery when phone woke up for alarm and battery is empty.
 * Note that actually user has little bit more because battery level is not checked
 * during first wakeup minute.
 */
#define ALARM_DELAYED_TIMEOUT 60

typedef enum {
  BATTERY_STATUS_FULL,
  BATTERY_STATUS_NORMAL,
  BATTERY_STATUS_LOW,
  BATTERY_STATUS_WARNING,
  BATTERY_STATUS_EMPTY,

  BATTERY_STATUS_COUNT
} battery_status_t;

static const char* const battery_status_name[BATTERY_STATUS_COUNT] = {
  "FULL", "NORMAL", "LOW", "WARNING", "EMPTY"
};

typedef struct battery_levels_t {
    int min_level;     /* percentance */
    int polling_time;  /* Polling time in sec */
    bool wakeup;       /* Resume from suspend to check battery level */
}  battery_levels_t;

/* This is default config for battery levels
 * It can be overridden by external file
 * BATTERY_LEVEL_CONFIG_FILE
 */
static battery_levels_t levels[BATTERY_STATUS_COUNT] = {
    /* Min %, polling time */
    {  80, 300, false  }, /* Full    80 - 100, polling 5 mins */
    {  20, 180, false  }, /* Normal  20 - 79 */
    {  10, 120, true   }, /* Low     10 - 19 */
    {   3,  60, true   }, /* Warning  3 -  9, shutdown happens below this */
    {   0,  60, true   }  /* Empty    0 -  2, shutdown should have happened already  */
};

typedef struct battery_state_t {
    int  percentance; /* Battery charging level 0..100 % */
    bool is_charging; /* True = charging (charger connected) */
    bool data_uptodate;
    battery_status_t status;
} battery_state_t;


static battery_state_t battery_state;
static dsme_state_t dsme_state = DSME_STATE_NOT_SET;
static bool battery_temp_normal = true;
static bool alarm_active = false;
static dsme_timer_t alarm_delayed_empty_timer = 0;

static int delayed_empty_fn(void* unused);

static void read_config_file(void)
{
  /* If external config file exists, then read it and use
   * values defined there for battery level and polling times
   */
    FILE* f;
    bool  success = true;
    int   i;
    battery_levels_t new_levels[BATTERY_STATUS_COUNT];

    // dsme_log(LOG_DEBUG, "batterytracker: %s()",__FUNCTION__);

    memset(new_levels, 0, sizeof new_levels);

    f = fopen(BATTERY_LEVEL_CONFIG_FILE, "r");
    if (f) {
        for (i = 0; i < BATTERY_STATUS_COUNT; i++) {
	    int wakeup = 0;
	    int values = fscanf(f, "%d, %d, %d",
				&new_levels[i].min_level,
				&new_levels[i].polling_time,
				&wakeup);
	    if( values < 2 ) {
                success = false;
	    }
	    else {
		if( values < 3 )
		    new_levels[i].wakeup = (i >= BATTERY_STATUS_LOW);
		else
		    new_levels[i].wakeup = (wakeup != 0);

                /* Do some sanity checking for values 
                  * Battery level values should be between 1-99, and in descending order.
                  * Polling times should also make sense  10-1000s
                  */
                if (((i <  BATTERY_STATUS_EMPTY) && (new_levels[i].min_level < 1)) ||
                    (new_levels[i].min_level > 99 ) ||
                    ((i>0) && (new_levels[i].min_level >=  new_levels[i-1].min_level)) ||
                    (new_levels[i].polling_time < 10 ) ||
                    (new_levels[i].polling_time > 1000 )) {
                    success = false;
                }
            }
            if (!success) {
                dsme_log(LOG_ERR, "batterytracker: syntax error in %s line %d", BATTERY_LEVEL_CONFIG_FILE, i+1);
                break;
            }
        }
        fclose(f);
    } else
        success = false;

    if (success) {
        memcpy(levels, new_levels, sizeof(levels));
        dsme_log(LOG_INFO, "batterytracker: Read new battery level values from %s", BATTERY_LEVEL_CONFIG_FILE);
    } else {
        dsme_log(LOG_DEBUG, "batterytracker: Using internal values for battery level");
    }
    dsme_log(LOG_DEBUG, "batterytracker: Shutdown limit is < %d%%", levels[BATTERY_STATUS_WARNING].min_level);
}

static bool read_data(const char *path, int *data)
{
    FILE* f;
    bool  ret = false;
    // dsme_log(LOG_DEBUG, "batterytracker: %s (%s)",__FUNCTION__, path);

    f = fopen(path, "r");
    if (f) {
        if (fscanf(f, "%d", data) == 1) {
            // dsme_log(LOG_DEBUG, "batterytracker: %s = %d", path, *data);
            ret = true;
        }
        fclose(f);
    } else {
        dsme_log(LOG_ERR, "batterytracker: FAILED to read %s", path);
    }
    return ret;
}



static void update_battery_info(void)
{
    int data;
    // dsme_log(LOG_DEBUG, "batterytracker: %s()", __FUNCTION__);

    battery_state.data_uptodate = false;
    if (read_data(BATTERY_LEVEL_PATH, &data)) {
        if ((data >= 0) && (data <= 100)) {
            battery_state.percentance = data;
            battery_state.data_uptodate = true;
        }
    }
    if (read_data(CHARGING_INFO_PATH, &data)) {
        battery_state.is_charging = (data != 0);
    }
    // dsme_log(LOG_DEBUG, "batterytracker: percentance = %d%%, is-charging = %s", battery_state.percentance, battery_state.is_charging ? "true" : "false");
}

static void update_battery_status(void)
{
    int n;
    // dsme_log(LOG_DEBUG, "batterytracker: %s()", __FUNCTION__);

    for (n = 0; n < BATTERY_STATUS_COUNT; n++) {
        if (battery_state.percentance >= levels[n].min_level) {
            battery_state.status = n;
            break;
        }
    } 
    if (n < BATTERY_STATUS_COUNT) {
        dsme_log(LOG_DEBUG, "batterytracker: Battery status %d%% = %s", battery_state.percentance, battery_status_name[n]);
    } else {
        dsme_log(LOG_ERR, "batterytracker: Code has logical problem, should never reach this line");
    }
}

static void  give_warning_if_needed()
{
    static bool battery_warning_sent = false;

    // dsme_log(LOG_DEBUG, "batterytracker: %s()", __FUNCTION__);
    
    if (battery_state.status == BATTERY_STATUS_WARNING) {
        if (! battery_warning_sent) {
            battery_warning_sent = true;
            dsme_log(LOG_INFO, "batterytracker:  WARNING, Battery level low %d%%", battery_state.percentance);
        }
        /* TODO: We should broadcast warnings every now and then  */
        /* Second tough, maybe we don't need to send anything. Seems that home screen gets
         * needed info already.
         * Let's leave this function here as place holder if ever needed
         */
    } else if (battery_warning_sent && (battery_state.status != BATTERY_STATUS_EMPTY)) {
        dsme_log(LOG_INFO, "batterytracker: Battery level back to normal %d%%", battery_state.percentance);
        battery_warning_sent = false;
    }
}

static void send_battery_empty_status(bool empty)
{
    dsme_log(LOG_DEBUG, "batterytracker: %s(%s)", __FUNCTION__, empty ? "empty" : "not empty");
    DSM_MSGTYPE_SET_BATTERY_STATE msg =
      DSME_MSG_INIT(DSM_MSGTYPE_SET_BATTERY_STATE);

    msg.empty = empty;

    broadcast_internally(&msg);
}

static void send_empty_if_needed()
{
    static bool battery_empty_sent = false;
    static bool battery_empty_seen = false;

    bool request_shutdown = false;

    // dsme_log(LOG_DEBUG, "batterytracker: %s()", __FUNCTION__);

    if (battery_state.status == BATTERY_STATUS_EMPTY) {
        dsme_log(LOG_DEBUG, "batterytracker: Battery level %d%% EMPTY", battery_state.percentance);
        if (! battery_empty_seen) {
            /* We have first time reached battery level empty */
            battery_empty_seen = true;
            dsme_log(LOG_INFO, "batterytracker: Battery low level seen at %d%%", battery_state.percentance);
        }
        /* Before starting shutdown, check charging. If charging is
         * going, give battery change to charge. But if battery level
         * keeps dropping even charger is connected (could be that we get only 100 mA)
         * we need to do shutdown. But let charging continue in actdead state
         */
        if (!battery_state.is_charging) {
            /* Charging is not goig on. Request shutdown */
            request_shutdown = true;

            /* Except when phone woke up and alarm is active.
             * In that case we wait couple of minutes extra before we report empty
             */
            if ((dsme_state == DSME_STATE_ACTDEAD) && alarm_active) {
                if (! alarm_delayed_empty_timer) {
                    alarm_delayed_empty_timer = dsme_create_timer(ALARM_DELAYED_TIMEOUT,
                                                                  delayed_empty_fn, NULL);
                    dsme_log(LOG_INFO, "batterytracker: Battery empty but shutdown delayed because of active alarm");
                }
                if (alarm_delayed_empty_timer)
                    request_shutdown = false;
            }
        } else if (dsme_state != DSME_STATE_ACTDEAD) {
            /* If charging in USER state, make sure level won't drop too much, keep min 1% */
            if (battery_state.percentance < 1) {
                request_shutdown = true;
                dsme_log(LOG_INFO, "batterytracker: Battery level keeps dropping. Must shutdown");
            } else {
                dsme_log(LOG_DEBUG, "batterytracker: Charging is going on. We don't shutdown");
            }
        } else {
            /* If charging in ACTDEAD state, let it charge even if not enough power is coming.
             * There is no point of shutting down because usb connection would wake up the device anyway
             * and that would result reboot loop. Better stay still in actdead and hope we get
             * some juice from charger.
             */
            dsme_log(LOG_DEBUG, "batterytracker: Charging in ACTDEAD. We don't shutdown");
        }

        if (request_shutdown && !battery_empty_sent) {
            dsme_log(LOG_INFO, "batterytracker: WARNING, Low battery shutdown, battery level %d%% EMPTY", battery_state.percentance);
            send_battery_empty_status(true);
            battery_empty_sent = true;
        }

    } else if (battery_empty_seen) {
        battery_empty_seen = false;
        dsme_log(LOG_INFO, "batterytracker: Battery no more empty %d%%", battery_state. percentance);
        if (battery_empty_sent) {
            send_battery_empty_status(false);
            battery_empty_sent = false;
        }
    }
}

static void schedule_next_wakeup(void)
{
    // dsme_log(LOG_DEBUG, "batterytracker: %s()", __FUNCTION__);

    int maxtime = 60;  /* Default polling time if no data or cold battery */
    int mintime = 30;
    bool wakeup = true; /* Default to resuming for battery monitoring */

    DSM_MSGTYPE_WAIT msg = DSME_MSG_INIT(DSM_MSGTYPE_WAIT);

    /* If data is not available or battery is too cold, then we use default polling */
    /* In normal case we use configured polling timeouts */
    if (battery_state.data_uptodate && battery_temp_normal) {
	wakeup  = levels[battery_state.status].wakeup;
        maxtime = levels[battery_state.status].polling_time;
        /* Note, it is important to give big enough window min..max
         * then IPHB can freely choose best wake-up time
         */
        mintime = maxtime/2;
    }

    msg.req.mintime = mintime;
    msg.req.maxtime = maxtime;
    msg.req.pid     = 0;
    msg.req.wakeup  = wakeup;
    msg.data        = 0;

    dsme_log(LOG_DEBUG, "batterytracker: next wakeup at %d...%d", msg.req.mintime,  msg.req.maxtime);

    broadcast_internally(&msg);
}

static void do_regular_duties()
{
    // dsme_log(LOG_DEBUG, "batterytracker: %s()", __FUNCTION__);

    update_battery_info();
    if (battery_state.data_uptodate) {
        update_battery_status();
        give_warning_if_needed();
        send_empty_if_needed();
    } else {
        dsme_log(LOG_WARNING, "batterytracker: No battery data available");
    }
    schedule_next_wakeup();
}

DSME_HANDLER(DSM_MSGTYPE_SET_THERMAL_STATUS, server, msg)
{
    const char *temp_status;

    if (msg->status == DSM_THERMAL_STATUS_LOWTEMP) {
        temp_status = "low temp warning";
        battery_temp_normal = false;
    } else {
        temp_status = "normal";  /* For our purpose also high temp is normal */
        battery_temp_normal = true;
    }

    dsme_log(LOG_DEBUG,
             "batterytracker: temp state: %s received", temp_status);
    if (! battery_temp_normal)
        schedule_next_wakeup();
}

DSME_HANDLER(DSM_MSGTYPE_STATE_CHANGE_IND, server, msg)
{
    dsme_log(LOG_DEBUG, "batterytracker: Received new state %d %s", msg->state,
             (msg->state == DSME_STATE_ACTDEAD) ? "ACTDEAD" :
             (msg->state == DSME_STATE_USER) ? "USER" : "");

    dsme_state = msg->state;
}

DSME_HANDLER(DSM_MSGTYPE_SET_ALARM_STATE, conn, msg)
{
    dsme_log(LOG_DEBUG,
             "batterytracker: alarm %s state received",
             msg->alarm_set ? "set" : "not set");
    alarm_active = msg->alarm_set;

    /* When alarm was active, we might have postponed shutdown.
     * Check current status now and stop possible timer
     */
    if ((!alarm_active) && alarm_delayed_empty_timer) {
        dsme_destroy_timer(alarm_delayed_empty_timer);
        alarm_delayed_empty_timer = 0;
        dsme_log(LOG_INFO, "batterytracker: Empty state was earlier delayed due alarm. Now alarm is off");
        send_empty_if_needed();
    }
}

DSME_HANDLER(DSM_MSGTYPE_WAKEUP, client, msg)
{
    dsme_log(LOG_DEBUG, "batterytracker: WAKEUP");
    do_regular_duties();
}


module_fn_info_t message_handlers[] = {
    DSME_HANDLER_BINDING(DSM_MSGTYPE_WAKEUP),
    DSME_HANDLER_BINDING(DSM_MSGTYPE_STATE_CHANGE_IND),
    DSME_HANDLER_BINDING(DSM_MSGTYPE_SET_THERMAL_STATUS),
    DSME_HANDLER_BINDING(DSM_MSGTYPE_SET_ALARM_STATE),
    { 0 }
};

static int delayed_empty_fn(void* unused)
{
    alarm_delayed_empty_timer = 0;
    alarm_active = false;
    dsme_log(LOG_INFO, "batterytracker: Alarm hold off timeout is over");
    send_empty_if_needed();
    return 0; /* stop the interval */
}


static void query_current_state(void)
{
    // dsme_log(LOG_DEBUG, "batterytracker: %s()", __FUNCTION__);
    DSM_MSGTYPE_STATE_QUERY query = DSME_MSG_INIT(DSM_MSGTYPE_STATE_QUERY);
    broadcast_internally(&query);
}

void module_init(module_t* handle)
{
    dsme_log(LOG_DEBUG, "batterytracker.so loaded");
    battery_state.data_uptodate = false;
    read_config_file();
    query_current_state();
    /* Schedule first reading to happen after one minute */
    schedule_next_wakeup();
}

void module_fini(void)
{
    dsme_log(LOG_DEBUG, "batterytracker.so unloaded");
}
