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
}  battery_levels_t;

/* This is default config for battery levels
 * It can be overridden by external file
 * BATTERY_LEVEL_CONFIG_FILE
 */
static battery_levels_t levels[BATTERY_STATUS_COUNT] = {
    /* Min %, polling time */
    {  80, 300  }, /* Full    80 - 100, polling 5 mins */
    {  20, 180  }, /* Normal  20 - 80 */
    {  10, 120  }, /* Low     10 - 20 */
    {   5, 60   }, /* Warning  5 - 10, shutdown happens below this */
    {   0, 60   }  /* Empty    0 - 5,  shutdown should have happened already  */
};

typedef struct battery_state_t {
    int  percentance; /* Battery charging level 0..100 % */
    bool is_charging; /* True = charging (charger connected) */
    bool data_uptodate;
    battery_status_t status;
} battery_state_t;


static battery_state_t battery_state;

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

    f = fopen(BATTERY_LEVEL_CONFIG_FILE, "r");
    if (f) {
        for (i = 0; i < BATTERY_STATUS_COUNT; i++) {
            if (fscanf(f,
                      "%d, %d",
                       &new_levels[i].min_level,
                       &new_levels[i].polling_time) != 2) {
                success = false;
            }
            if (success) {
                /* Do some sanity checking for values 
                  * Battery level values should be between 2-99, and in descending order.
                  * Polling times should also make sense  10-1000s
                  */
                if (((i <  BATTERY_STATUS_EMPTY) && (new_levels[i].min_level < 2)) ||
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
    static int  battery_level_when_empty_seen = 0;

    bool request_shutdown = false;

    // dsme_log(LOG_DEBUG, "batterytracker: %s()", __FUNCTION__);

    if (battery_state.status == BATTERY_STATUS_EMPTY) {
        dsme_log(LOG_INFO, "batterytracker: WARNING, Battery level %d%% EMPTY", battery_state.percentance);
        if (! battery_empty_seen) {
            /* We have first time reached battery level empty.
             * Remember the level we saw it
             */
            battery_empty_seen = true;
            battery_level_when_empty_seen = battery_state.percentance;
        }
        /* Before starting shutdown, check charging. If charging is
         * going, give battery change to charge. But if battery level
         * keeps dropping even charger is connected (could be that we get only 100 mA)
         * we need to do shutdown, no matter what
         */
        if (!battery_state.is_charging) {
            /* Charging is not goig on. Request shutdown */
            request_shutdown = true;
        } else {
            /* If charging, make sure level won't drop more and always keep above 2% */
            if ((battery_state.percentance < battery_level_when_empty_seen) ||
                (battery_state.percentance < 2)) {
                request_shutdown = true;
                dsme_log(LOG_INFO, "batterytracker: Battery level keeps dropping. Must shutdown");
            } else {
                dsme_log(LOG_INFO, "batterytracker: Charging is going on. We don't shutdown");
            }
        }

        if (request_shutdown && !battery_empty_sent) {
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

    int maxtime = 60;  /* Default polling time if no data */
    int mintime = 30;

    DSM_MSGTYPE_WAIT msg = DSME_MSG_INIT(DSM_MSGTYPE_WAIT);

    if (battery_state.data_uptodate) {
        maxtime = levels[battery_state.status].polling_time;
        /* Note, it is important to give big enough window min..max
         * then IPHB can freely choose best wake-up time
         */
        mintime = maxtime/2;
    }

    msg.req.mintime = mintime;
    msg.req.maxtime = maxtime;
    msg.req.pid     = 0;
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

DSME_HANDLER(DSM_MSGTYPE_WAKEUP, client, msg)
{
    dsme_log(LOG_DEBUG, "batterytracker: WAKEUP");
    do_regular_duties();
}

DSME_HANDLER(DSM_MSGTYPE_DBUS_CONNECT, client, msg)
{
    dsme_log(LOG_DEBUG, "batterytracker: DBUS_CONNECT");
    /* Start working */
    do_regular_duties();
}

DSME_HANDLER(DSM_MSGTYPE_DBUS_DISCONNECT, client, msg)
{
    dsme_log(LOG_DEBUG, "batterytracker: DBUS_DISCONNECT");
}

module_fn_info_t message_handlers[] = {
    DSME_HANDLER_BINDING(DSM_MSGTYPE_WAKEUP),
    DSME_HANDLER_BINDING(DSM_MSGTYPE_DBUS_CONNECT),
    DSME_HANDLER_BINDING(DSM_MSGTYPE_DBUS_DISCONNECT),
    { 0 }
};


void module_init(module_t* handle)
{
    /* Wait for DSM_MSGTYPE_DBUS_CONNECT
       before actually doing anything
       We don't need dbus but that is good timing point
       to start this.
     */
    dsme_log(LOG_DEBUG, "batterytracker.so loaded");
    battery_state.data_uptodate = false;
    read_config_file();
}

void module_fini(void)
{
    dsme_log(LOG_DEBUG, "batterytracker.so unloaded");
}
