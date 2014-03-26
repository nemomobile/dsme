/**
   @file bootreasonlogger.c

   Log the system shutdown and powerup reasons.

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

#include "dsme/modules.h"
#include "dsme/logging.h"

#include <dsme/state.h>
#include <dsme/protocol.h>

#include <stdio.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>

#define BOOT_LOG_FILE   "/var/log/systemboot.log"
#define PFIX            "bootlogger: "
#define MAX_CMDLINE_LEN 1024

typedef enum {
  SD_REASON_UNKNOWN,
  SD_SW_REBOOT,
  SD_DBUS_FAILED_REBOOT,
  SD_SW_SHUTDOWN,
  SD_DEVICE_OVERHEAT,
  SD_BATTERY_EMPTY,
  SD_USER_PWR_KEY,

  SD_REASON_COUNT
} shutdown_reason_t;

static const char* const shutdown_reason_string[SD_REASON_COUNT] = {
  "Reason Unknown",
  "SW reboot request",
  "Dbus failed, reboot",
  "SW shutdown request",
  "Device overheated",
  "Battery empty",
  "User Power Key"
};

static shutdown_reason_t saved_shutdown_reason = SD_REASON_UNKNOWN;

/* Powerup reason is either in enviroment or in kernel commandline
 * we will look for all these and in this order
 */
static const char *possible_pwrup_strings[] = {
  "pwr_on_status",
  "bootreason",
  "bootup_reason",
  "androidboot.mode",
  0
};

static char pwrup_reason[80];

static const struct {
    int         value;
    const char* name;
} states[] = {
#define DSME_STATE(STATE, VALUE) { VALUE, #STATE },
#include <dsme/state_states.h>
#undef  DSME_STATE
};

static const char* state_name(dsme_state_t state)
{
    int         index_sn;
    const char* name = "*** UNKNOWN STATE ***";;

    for (index_sn = 0; index_sn < sizeof states / sizeof states[0]; ++index_sn) {
        if (states[index_sn].value == state) {
            name = states[index_sn].name;
            break;
        }
    }

    return name;
}

static bool sw_update_running(void)
{
    return (access("/tmp/os-update-running", F_OK) == 0);
}

static bool system_still_booting(void)
{
    /* Once system boot is over, init-done flag is set */
    /* If file is not there, we are still booting */ 
    return (access("/run/systemd/boot-status/init-done", F_OK) != 0);
}

static bool dbus_has_failed(void)
{
    /* If dbus fails, dsme dbus has noticed it, marked and requested reboot */
    return (access("/run/systemd/boot-status/dbus-failed", F_OK) == 0);
}

static const char * get_timestamp(void)
{
    static const char default_date[] = "00000000_000000";
    static char date_time[80];
    time_t raw_time;
    struct tm *timeinfo;
    const char *timestamp = default_date;

    if ((time(&raw_time) > 0) &&
        ((timeinfo = localtime(&raw_time)) != NULL) &&
        (strftime(date_time, sizeof(date_time), "%Y%m%d_%H%M%S", timeinfo) > 0)) {
            timestamp = date_time;
    } else 
        dsme_log(LOG_ERR, PFIX"failed to get timestamp");

    return timestamp;
}

static void write_log(const char *state, const char *reason)
{
    FILE* f;
    bool  success = false;

    f = fopen(BOOT_LOG_FILE, "a");
    if (f) {
      if ( fprintf(f, "%s %s %s\n", get_timestamp(), state, reason) > 0) {
            success = true;
            sync();
        }
        fclose(f);
    }

    if (! success) {
        dsme_log(LOG_ERR, PFIX"can't write into %s", BOOT_LOG_FILE);
    }
}


static int get_cmd_line_value(char* get_value, int max_len, const char* key)
{
    FILE*       cmdline_file;
    char        cmdline[MAX_CMDLINE_LEN];
    int         ret = -1;
    int         keylen;
    char*       key_and_value;
    char*       value;

    cmdline_file = fopen("/proc/cmdline", "r");
    if(!cmdline_file) {
        dsme_log(LOG_ERR, PFIX"Could not open /proc/cmdline\n");
        return -1;
    }

    if (fgets(cmdline, MAX_CMDLINE_LEN, cmdline_file)) {
        key_and_value = strtok(cmdline, " ");
        keylen = strlen(key);
        while (key_and_value != NULL) {
            if(!strncmp(key_and_value, key, keylen)) {
                value = strtok(key_and_value, "=");
                value = strtok(NULL, "=");
                if (value) {
                    snprintf(get_value, max_len, "%s", value);
                    ret = strlen(get_value);
                }
                break;
            }
            key_and_value = strtok(NULL, " ");
        }
    }
    fclose(cmdline_file);
    return ret;
}

static char * get_powerup_reason_str(void)
{
    char *env;
    const char *search_key;
    int i = 0;
    char cmdvalue[80];

    /* set default */
    snprintf(pwrup_reason, sizeof(pwrup_reason), "Reason Unknown");

    /* Powerup reason is either in enviroment or in kernel commandline
     * we will look both and use first match
     */

    while ((search_key = possible_pwrup_strings[i])) {
        if ((env = getenv(search_key))) {
            snprintf(pwrup_reason, sizeof(pwrup_reason),"%s=%s", search_key, env);
            break;
        } else if ((get_cmd_line_value(cmdvalue, sizeof(cmdvalue), search_key)) > 0) {
            snprintf(pwrup_reason, sizeof(pwrup_reason),"%s=%s", search_key, cmdvalue);
            break;
        }
        i++;
    }
    return pwrup_reason;
}


static void log_startup(void)
{
    if (system_still_booting())
        write_log("Startup: ", get_powerup_reason_str());
    else {
        /* System has already booted. We are here because
         * dsme daemon has been restarted
         */
        write_log("Startup: ", "dsme daemon restarted, not real system startup");
    }
}

static void log_shutdown(void)
{
    if (sw_update_running())
        write_log("Shutdown:", "SW update reboot");
    else
        write_log("Shutdown:", shutdown_reason_string[saved_shutdown_reason]);
}

DSME_HANDLER(DSM_MSGTYPE_SET_BATTERY_STATE, conn, battery)
{
    dsme_log(LOG_DEBUG,
             PFIX"battery %s state received",
             battery->empty ? "empty" : "not empty");

    write_log("Received: battery ", battery->empty ? "empty" : "not empty");
    if (battery->empty)
        saved_shutdown_reason = SD_BATTERY_EMPTY;
    else // Battery is no more empty. Shutdown won't happen
        saved_shutdown_reason = SD_REASON_UNKNOWN;
}

DSME_HANDLER(DSM_MSGTYPE_SET_THERMAL_STATUS, conn, msg)
{
    bool overheated = false;
    const char *temp_status;
    char  str[80];

    if (msg->status == DSM_THERMAL_STATUS_OVERHEATED) {
        temp_status = "overheated";
        overheated = true;
    } else if (msg->status == DSM_THERMAL_STATUS_LOWTEMP)
        temp_status = "low warning";
    else
        temp_status = "normal";

    dsme_log(LOG_DEBUG,
             PFIX"temp (%s) state: %s (%dC)", msg->sensor_name, temp_status, msg->temperature);
    snprintf(str, sizeof(str), "device (%s) temp status %s (%dC)", msg->sensor_name, temp_status, msg->temperature);
    write_log("Received:", str);
    if (overheated)
        saved_shutdown_reason = SD_DEVICE_OVERHEAT;
    else  // Device is no more overheated. Shutdown won't happen
        saved_shutdown_reason = SD_REASON_UNKNOWN;
}

DSME_HANDLER(DSM_MSGTYPE_REBOOT_REQ, conn, msg)
{
    char* sender = endpoint_name(conn);

    write_log("Received: reboot request from", sender ? sender : "(unknown)");
    if (saved_shutdown_reason == SD_REASON_UNKNOWN) {
        if (dbus_has_failed())
            saved_shutdown_reason = SD_DBUS_FAILED_REBOOT;
        else
            saved_shutdown_reason = SD_SW_REBOOT;
    }
    free(sender);
}

DSME_HANDLER(DSM_MSGTYPE_SHUTDOWN_REQ, conn, msg)
{
    char* sender = endpoint_name(conn);

    write_log("Received: shutdown request from", sender ? sender : "(unknown)");
    if (saved_shutdown_reason == SD_REASON_UNKNOWN) {
        if (sender && (strstr(sender, "/mce") != NULL))
            saved_shutdown_reason = SD_USER_PWR_KEY;
        else
            saved_shutdown_reason = SD_SW_SHUTDOWN;
    }
    free(sender);
}

DSME_HANDLER(DSM_MSGTYPE_STATE_CHANGE_IND, conn, msg)
{
    write_log("Received: dsme internal state", state_name(msg->state));
    if (saved_shutdown_reason == SD_REASON_UNKNOWN) {
        if (msg->state == DSME_STATE_SHUTDOWN)
            saved_shutdown_reason = SD_SW_SHUTDOWN;
        else if (msg->state == DSME_STATE_REBOOT)
            saved_shutdown_reason = SD_SW_REBOOT;
    }
}

module_fn_info_t message_handlers[] = {
    DSME_HANDLER_BINDING(DSM_MSGTYPE_SHUTDOWN_REQ),
    DSME_HANDLER_BINDING(DSM_MSGTYPE_REBOOT_REQ),
    DSME_HANDLER_BINDING(DSM_MSGTYPE_SET_THERMAL_STATUS),
    DSME_HANDLER_BINDING(DSM_MSGTYPE_SET_BATTERY_STATE),
    DSME_HANDLER_BINDING(DSM_MSGTYPE_STATE_CHANGE_IND),
    {0}
};


void module_init(module_t* handle)
{
    dsme_log(LOG_DEBUG, "bootreasonlogger.so loaded");
    log_startup();
    saved_shutdown_reason = SD_REASON_UNKNOWN;
}

void module_fini(void)
{
    log_shutdown();
    dsme_log(LOG_DEBUG, "bootreasonlogger.so unloaded");
}
