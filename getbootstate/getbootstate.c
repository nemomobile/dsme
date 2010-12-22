/**
   @file getbootstate.c

   The getbootstate tool
   <p>
   Copyright (C) 2007-2010 Nokia Corporation.

   @author Peter De Schrijver <peter.de-schrijver@nokia.com>
   @author Semi Malinen <semi.malinen@nokia.com>
   @author Matias Muhonen <ext-matias.muhonen@nokia.com>

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

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <time.h>

#define MAX_BOOTREASON_LEN   40
#define MAX_REBOOT_COUNT_LEN 40
#define MAX_SAVED_STATE_LEN  40

#define DEFAULT_MAX_BOOTS           5
#define DEFAULT_MIN_BOOT_TIME     120 // seconds ==  2 minutes

#define DEFAULT_MAX_WD_RESETS       6
#define DEFAULT_MIN_WD_RESET_TIME 600 // seconds == 10 minutes

#define BOOT_LOOP_COUNT_PATH "/var/lib/dsme/boot_count"
#define SAVED_STATE_PATH     "/var/lib/dsme/saved_state"

#define BOOT_REASON_UNKNOWN         "unknown"
#define BOOT_REASON_SWDG_TIMEOUT    "swdg_to"
#define BOOT_REASON_SEC_VIOLATION   "sec_vio"
#define BOOT_REASON_32K_WDG_TIMEOUT "32wd_to"
#define BOOT_REASON_POWER_ON_RESET  "por"
#define BOOT_REASON_POWER_KEY       "pwr_key"
#define BOOT_REASON_MBUS            "mbus"
#define BOOT_REASON_CHARGER         "charger"
#define BOOT_REASON_USB             "usb"
#define BOOT_REASON_SW_RESET        "sw_rst"
#define BOOT_REASON_RTC_ALARM       "rtc_alarm"
#define BOOT_REASON_NSU             "nsu"

#define BOOT_MODE_UPDATE_MMC "update"
#define BOOT_MODE_LOCAL      "local"
#define BOOT_MODE_TEST       "test"
#define BOOT_MODE_NORMAL     "normal"

#define DEFAULT_CMDLINE_PATH "/proc/cmdline"
#define MAX_CMDLINE_LEN      1024

#define GETBOOTSTATE_PREFIX "getbootstate: "


static bool forcemode = false;

static void log_msg(char* format, ...) __attribute__ ((format (printf, 1, 2)));

/**
 * get value from /proc/cmdline
 * @return 0 upon success, -1 otherwise
 * @note expected format in cmdline key1=value1 key2=value2, key3=value3
 *       key-value pairs separated by space or comma.
 *       value after key separated with equal sign '='
 **/
static int get_cmd_line_value(char* get_value, int max_len, char* key)
{
    const char* cmdline_path;
    FILE*       cmdline_file;
    char        cmdline[MAX_CMDLINE_LEN];
    int         ret = -1;
    int         keylen;
    char*       key_and_value;
    char*       value;

    cmdline_path = getenv("CMDLINE_PATH");
    cmdline_path = cmdline_path ? cmdline_path : DEFAULT_CMDLINE_PATH;

    cmdline_file = fopen(cmdline_path, "r");
    if(!cmdline_file) {
        log_msg("Could not open %s\n", cmdline_path);
        return -1;
    }

    if (fgets(cmdline, MAX_CMDLINE_LEN, cmdline_file)) {
        key_and_value = strtok(cmdline, " ,");
        keylen = strlen(key);
        while (key_and_value != NULL) {
            if(!strncmp(key_and_value, key, keylen)) {
                value = strtok(key_and_value, "=");
                value = strtok(NULL, "=");
                if (value) {
                    strncpy(get_value, value, max_len);
                    ret = 0;
                }
                break;
            }
            key_and_value = strtok(NULL, " ,");
        }
    }
    fclose(cmdline_file);
    return ret;
}

static int get_bootmode(char* bootmode, int max_len)
{
    return get_cmd_line_value(bootmode, max_len, "bootmode=");
}

static int get_bootreason(char* bootreason, int max_len)
{
    return get_cmd_line_value(bootreason, max_len, "bootreason=");
}

static void log_msg(char* format, ...)
{
    int     saved = errno; // preserve errno
    va_list ap;
    char    buffer[strlen(format) + strlen(GETBOOTSTATE_PREFIX) + 1];

    errno = saved;
    sprintf(buffer, "%s%s", GETBOOTSTATE_PREFIX, format);

    va_start(ap, format);
    vfprintf(stderr,buffer,ap);
    va_end(ap);
    errno = saved;
}


static int save_state(const char* state)
{
    FILE* saved_state_file;

    saved_state_file = fopen(SAVED_STATE_PATH ".new", "w");
    if(!saved_state_file) {
        log_msg("Could not open " SAVED_STATE_PATH ".new - %s\n",
                strerror(errno));
        return -1;
    }

    errno = 0;
    if(fwrite(state, 1, strlen(state), saved_state_file) <= 0) {
        log_msg("Could not write state" " - %s\n", strerror(errno));
        fclose(saved_state_file);
        return -1;
    }

    fflush(saved_state_file);
    if(fsync(fileno(saved_state_file)) < 0) {
        log_msg("Could not sync data" " - %s\n", strerror(errno));
        fclose(saved_state_file);
        return -1;
    }

    if(fclose(saved_state_file) < 0) {
        log_msg("Could not write state" " - %s\n", strerror(errno));
        return -1;
    }
    if(rename(SAVED_STATE_PATH ".new", SAVED_STATE_PATH)) {
        log_msg("Could not rename " SAVED_STATE_PATH ".new to "
                SAVED_STATE_PATH " - %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

static char* get_saved_state(void)
{
    FILE* saved_state_file;
    char  saved_state[MAX_SAVED_STATE_LEN];
    char* ret;

    saved_state_file = fopen(SAVED_STATE_PATH, "r");
    if(!saved_state_file) {
        log_msg("Could not open " SAVED_STATE_PATH " - %s\n",
                strerror(errno));
        return "USER";
    }

    if(!fgets(saved_state, MAX_SAVED_STATE_LEN, saved_state_file)) {
        log_msg("Reading " SAVED_STATE_PATH " failed" " - %s\n",
                strerror(errno));
        fclose(saved_state_file);
        return "USER";
    }

    fclose(saved_state_file);

    ret = strdup(saved_state);

    if(!ret) {
        return "USER";
    } else {
        return ret;
    }
}


static void write_loop_counts(unsigned boots, unsigned wd_resets, time_t when)
{
    FILE* f;

    if ((f = fopen(BOOT_LOOP_COUNT_PATH, "w")) == 0) {
        log_msg("Could not open " BOOT_LOOP_COUNT_PATH ": %s\n",
                strerror(errno));
    } else {
        if (fprintf(f, "%lu %u %u", when, boots, wd_resets) < 0) {
            log_msg("Error writing " BOOT_LOOP_COUNT_PATH "\n");
        } else if (ferror(f) || fflush(f) == EOF) {
            log_msg("Error flushing " BOOT_LOOP_COUNT_PATH ": %s\n",
                    strerror(errno));
        } else if (fsync(fileno(f)) == -1) {
            log_msg("Error syncing " BOOT_LOOP_COUNT_PATH ": %s\n",
                    strerror(errno));
        }

        if (fclose(f) == EOF) {
            log_msg("Error closing " BOOT_LOOP_COUNT_PATH ": %s\n",
                    strerror(errno));
        }
    }
}

static void read_loop_counts(unsigned* boots, unsigned* wd_resets, time_t* when)
{
    *boots     = 0;
    *wd_resets = 0;
    *when      = 0;

    FILE* f;

    if ((f = fopen(BOOT_LOOP_COUNT_PATH, "r")) == 0) {
        log_msg("Could not open " BOOT_LOOP_COUNT_PATH ": %s\n",
                strerror(errno));
    } else {
        if (fscanf(f, "%lu %u %u", (unsigned long*)when, boots, wd_resets) != 3) {
            log_msg("Error reading file " BOOT_LOOP_COUNT_PATH);
        }
        (void)fclose(f);
    }
}

static unsigned get_env(const char* name, unsigned default_value)
{
    const char* e = getenv(name);
    return e ? atoi(e) : default_value;
}

typedef enum {
    RESET_COUNTS    = 0x0,
    COUNT_BOOTS     = 0x1,
    COUNT_WD_RESETS = 0x2,
    COUNT_ALL    = (COUNT_BOOTS | COUNT_WD_RESETS)
} LOOP_COUNTING_TYPE;

static void check_for_boot_loops(LOOP_COUNTING_TYPE count_type,
                                 const char**       malf_info)
{
    unsigned      boots;
    unsigned      wd_resets;
    time_t        now;
    time_t        last;
    unsigned long seconds;
    const char*   loop_malf_info = 0;
    unsigned      max_boots;
    unsigned      max_wd_resets;
    unsigned      min_boot_time;
    unsigned      min_wd_reset_time;

    time(&now);
    read_loop_counts(&boots, &wd_resets, &last);
    seconds = now - last;

    // Obtain limits
    max_boots         = get_env("GETBOOTSTATE_MAX_BOOTS",
                                DEFAULT_MAX_BOOTS);
    max_wd_resets     = get_env("GETBOOTSTATE_MAX_WD_RESETS",
                                DEFAULT_MAX_WD_RESETS);
    min_boot_time     = get_env("GETBOOTSTATE_MIN_BOOT_TIME",
                                DEFAULT_MIN_BOOT_TIME);
    min_wd_reset_time = get_env("GETBOOTSTATE_MIN_WD_RESET_TIME",
                                DEFAULT_MIN_WD_RESET_TIME);

    // Check for too many frequent and consecutive reboots
    if (count_type & COUNT_BOOTS) {
        if (seconds < min_boot_time) {
            if (++boots > max_boots) {
                // Detected a boot loop
                loop_malf_info = "unknown too frequent reboots";
                log_msg("%d reboots; loop detected\n", boots);
            } else {
                log_msg("Increased boot count to %d\n", boots);
            }
        } else {
            log_msg("%d s or more since last reboot; resetting counter\n",
                    min_boot_time);
            boots = 0;
        }
    } else {
        log_msg("Resetting boot counter\n");
        boots = 0;
    }

    // Check for too many frequent and consecutive WD resets
    if (count_type & COUNT_WD_RESETS) {
        if (seconds < min_wd_reset_time) {
            if (++wd_resets > max_wd_resets) {
                // Detected a WD reset loop
                loop_malf_info = "watchdog too frequent resets";
                log_msg("%d WD resets; loop detected\n", wd_resets);
            } else {
                log_msg("Increased WD reset count to %d\n", wd_resets);
            }
        } else {
            log_msg("%d s or more since last WD reset; resetting counter\n",
                    min_wd_reset_time);
            wd_resets = 0;
        }
    } else {
        log_msg("Resetting WD reset counter\n");
        wd_resets = 0;
    }

    if (loop_malf_info) {
        // Malf detected;
        // reset counts so that a reboot can be attempted after the MALF
        boots     = 0;
        wd_resets = 0;

        // Pass malf information to the caller if it doesn't have any yet
        if (malf_info && !*malf_info) {
            *malf_info = loop_malf_info;
        }
    }

    write_loop_counts(boots, wd_resets, now);
}

static void return_bootstate(const char*        bootstate,
                             const char*        malf_info,
                             LOOP_COUNTING_TYPE count_type)
{
    // Only save "normal" bootstates (USER, ACT_DEAD)
    if (forcemode) {
        static const char* saveable[] = { "USER", "ACT_DEAD", 0 };
        int i;

        for (i = 0; saveable[i]; ++i) {
            if (!strncmp(bootstate, saveable[i], strlen(saveable[i]))) {
                save_state(saveable[i]);
                break;
            }
        }
    }

    // Deal with possible startup loops
    if (forcemode) {
        check_for_boot_loops(count_type, &malf_info);
    }

    // Print the bootstate to console and exit
    if (forcemode && malf_info) {
        printf("%s %s\n", bootstate, malf_info);
    } else {
        puts(bootstate);
    }

    exit (0);
}

int main(int argc, char** argv)
{
    char bootreason[MAX_BOOTREASON_LEN];
    char bootmode[MAX_BOOTREASON_LEN];

    if (argc  > 1  && !strcmp(argv[1], "-f")) {
        forcemode = true;
    }

    if(!get_bootmode(bootmode, MAX_BOOTREASON_LEN)) {
        if(!strcmp(bootmode, BOOT_MODE_UPDATE_MMC)) {
            log_msg("Update mode requested\n");
            return_bootstate("FLASH", 0, COUNT_BOOTS);
        }
        if(!strcmp(bootmode, BOOT_MODE_LOCAL)) {
            log_msg("LOCAL mode requested\n");
            return_bootstate("LOCAL", 0, COUNT_BOOTS);
        }
        if(!strcmp(bootmode, BOOT_MODE_TEST)) {
            log_msg("TEST mode requested\n");
            return_bootstate("TEST", 0, COUNT_BOOTS);
        }
    }


    if(get_bootreason(bootreason, MAX_BOOTREASON_LEN) < 0) {
        log_msg("Bootreason could not be read\n");
        return_bootstate("MALF",
                         "SOFTWARE bootloader no bootreason",
                         COUNT_BOOTS);
    }


    if (!strcmp(bootreason, BOOT_REASON_SEC_VIOLATION)) {
        log_msg("Security violation\n");
        // TODO: check if "software bootloader" is ok
        return_bootstate("MALF",
                         "SOFTWARE bootloader security violation",
                         COUNT_BOOTS);
    }


    if (!strcmp(bootreason, BOOT_REASON_POWER_ON_RESET) ||
        !strcmp(bootreason, BOOT_REASON_SWDG_TIMEOUT)   ||
        !strcmp(bootreason, BOOT_REASON_32K_WDG_TIMEOUT))
    {
        char* saved_state;
        char* new_state;

        saved_state = get_saved_state();

        // We decided to select "USER" to prevent ACT_DEAD reboot loop
        new_state = "USER";

        log_msg("Unexpected reset occured (%s). "
                  "Previous bootstate=%s - selecting %s\n",
                bootreason,
                saved_state,
                new_state);

        LOOP_COUNTING_TYPE count;
        if (!strcmp(bootreason, BOOT_REASON_POWER_ON_RESET)) {
            count = RESET_COUNTS; // zero loop counters on power on reset
        } else {
            count = COUNT_ALL;
        }
        return_bootstate(new_state, 0, count);
    }

    if(!strcmp(bootreason,BOOT_REASON_SW_RESET))   {
        char* saved_state;

        /* User requested reboot.
         * Boot back to state where we were (saved_state).
         * But if normal mode was requested to get out of
         * special mode (like LOCAL or TEST),
         * then boot to USER mode
         */
        saved_state = get_saved_state();
        log_msg("User requested reboot (saved_state=%s, bootreason=%s)\n",
                saved_state,
                bootreason);
        if(strcmp(saved_state, "ACT_DEAD") &&
           strcmp(saved_state, "USER")     &&
           !strcmp(bootmode, BOOT_MODE_NORMAL))
        {
            log_msg("request was to NORMAL mode\n");
            return_bootstate("USER", 0, COUNT_BOOTS);
        } else {
            return_bootstate(saved_state, 0, COUNT_BOOTS);
        }
    }

    if(!strcmp(bootreason, BOOT_REASON_POWER_KEY)) {
        log_msg("User pressed power button\n");
        return_bootstate("USER", 0, RESET_COUNTS); // reset loop counters
    }
    if(!strcmp(bootreason, BOOT_REASON_NSU)) {
        log_msg("software update (NSU)\n");
        return_bootstate("USER", 0, COUNT_BOOTS);
    }

    if(!strcmp(bootreason, BOOT_REASON_CHARGER) ||
       !strcmp(bootreason, BOOT_REASON_USB))
    {
        log_msg("User attached charger\n");
        return_bootstate("ACT_DEAD", 0, COUNT_BOOTS);
    }

    if(!strcmp(bootreason, BOOT_REASON_RTC_ALARM)) {
        log_msg("Alarm wakeup occured\n");
        return_bootstate("ACT_DEAD", 0, COUNT_BOOTS);
    }

    log_msg("Unknown bootreason '%s' passed by nolo\n", bootreason);
    return_bootstate("MALF",
                     "SOFTWARE bootloader unknown bootreason to getbootstate",
                     COUNT_BOOTS);

    return 0; // never reached
}
