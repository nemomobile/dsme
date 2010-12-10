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

#define MAX_BOOTREASON_LEN   40
#define MAX_REBOOT_COUNT_LEN 40
#define MAX_SAVED_STATE_LEN  40

#define REBOOT_COUNT_PATH "/var/lib/dsme/boot_count"
#define SAVED_STATE_PATH  "/var/lib/dsme/saved_state"

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

#define CMDLINE_PATH    "/proc/cmdline"
#define MAX_CMDLINE_LEN 1024

#define GETBOOTSTATE_PREFIX "getbootstate: "


static  int  forcemode = 0;

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
    FILE* cmdline_file;
    char  cmdline[MAX_CMDLINE_LEN];
    int   ret = -1;
    int   keylen;
    char* key_and_value;
    char* value;

    cmdline_file = fopen(CMDLINE_PATH, "r");
    if(!cmdline_file) {
        log_msg("Could not open " CMDLINE_PATH "\n");
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

static void clear_reboot_count(void)
{
    FILE* reboot_count_file = 0;

    reboot_count_file = fopen(REBOOT_COUNT_PATH,"w");
    if (!reboot_count_file) {
        log_msg("Could not open " REBOOT_COUNT_PATH " - %s\n", strerror(errno));
        goto CLEANUP;
    }
    fputs("0\n",reboot_count_file);

    if (ferror(reboot_count_file) || fflush(reboot_count_file) == EOF) {
        log_msg("can't write %s: %s\n", REBOOT_COUNT_PATH, strerror(errno));
        goto CLEANUP;
    }

    if (fsync(fileno(reboot_count_file)) == -1) {
        log_msg("can't sync %s: %s\n", REBOOT_COUNT_PATH, strerror(errno));
    }

CLEANUP:
    if (reboot_count_file != 0 && fclose(reboot_count_file) == EOF) {
        log_msg("can't close %s: %s\n", REBOOT_COUNT_PATH, strerror(errno));
    }
}

static unsigned int read_reboot_count(time_t* first_reboot)
{
    FILE*        reboot_count_file = 0;
    char         reboot_count_str[MAX_REBOOT_COUNT_LEN];
    unsigned int reboot_count      = 0;
    char*        p;

    *first_reboot = 0;

    reboot_count_file = fopen(REBOOT_COUNT_PATH, "r");
    if (!reboot_count_file) {
        log_msg("Could not open " REBOOT_COUNT_PATH " - %s\n", strerror(errno));
        goto CLEANUP;
    }

    if (!fgets(reboot_count_str, MAX_REBOOT_COUNT_LEN, reboot_count_file)) {
        goto CLEANUP;
    }

    reboot_count = atoi(reboot_count_str);
    p = strchr(reboot_count_str,  ' ');
    if (p) {
        p++;
        *first_reboot = (time_t)strtoul(p,  0,  10);
    }

CLEANUP:
    if (reboot_count_file != 0 && fclose(reboot_count_file) == EOF) {
        log_msg("can't close %s: %s\n", REBOOT_COUNT_PATH, strerror(errno));
    }

    return reboot_count;
}

static unsigned int increment_reboot_count(void)
{
    unsigned int reboot_count      = 0;
    FILE*        reboot_count_file = 0;
    time_t       first_reboot;
    time_t       now               = time(0);

    reboot_count = read_reboot_count(&first_reboot);
    if (!first_reboot) {
        first_reboot = now;
    }

    reboot_count++;

    reboot_count_file = fopen(REBOOT_COUNT_PATH, "w");
    if (!reboot_count_file) {
        log_msg("Could not open " REBOOT_COUNT_PATH " - %s\n", strerror(errno));
        goto CLEANUP;
    }

    if (now < first_reboot) {
        first_reboot  = now - 1; // Some sanity!
    }

    fprintf(reboot_count_file,
            "%d %lu %lu\n",
            reboot_count,
            (unsigned long)first_reboot,
            (unsigned long)now);

    if (ferror(reboot_count_file) || fflush(reboot_count_file) == EOF) {
        log_msg("can't write %s: %s\n", REBOOT_COUNT_PATH, strerror(errno));
        goto CLEANUP;
    }

    if (fsync(fileno(reboot_count_file)) == -1) {
        log_msg("can't sync %s: %s\n", REBOOT_COUNT_PATH, strerror(errno));
    }

CLEANUP:
    if (reboot_count_file != 0 && fclose(reboot_count_file) == EOF) {
        log_msg("can't close %s: %s\n", REBOOT_COUNT_PATH, strerror(errno));
    }

    return reboot_count;
}

static int save_state(char* state)
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

static void return_bootstate(char* bootstate)
{
    // If bootstate is LOCAL, TEST, MALF or FLASH don't save the bootstate.
    if(forcemode                  &&
       strcmp(bootstate, "LOCAL") &&
       strcmp(bootstate, "TEST")  &&
       strcmp(bootstate, "MALF")  &&
       strcmp(bootstate, "FLASH"))
    {
        // We have a "normal" bootstate (USER, ACTDEAD) -> save the bootstate
        save_state(bootstate);
    }

    // Print the bootstate to console and exit
    puts(bootstate);

    exit (0);
}

int main(int argc, char** argv)
{
    char bootreason[MAX_BOOTREASON_LEN];
    char bootmode[MAX_BOOTREASON_LEN];

    if(!get_bootmode(bootmode, MAX_BOOTREASON_LEN)) {
        if(!strcmp(bootmode, BOOT_MODE_UPDATE_MMC)) {
            log_msg("Update mode requested\n");
            return_bootstate("FLASH");
        }
        if(!strcmp(bootmode, BOOT_MODE_LOCAL)) {
            log_msg("LOCAL mode requested\n");
            return_bootstate("LOCAL");
        }
        if(!strcmp(bootmode, BOOT_MODE_TEST)) {
            log_msg("TEST mode requested\n");
            return_bootstate("TEST");
        }
    }


    if(get_bootreason(bootreason, MAX_BOOTREASON_LEN) < 0) {
        log_msg("Bootreason could not be read\n");
        return_bootstate("MALF");
    }


    if (!strcmp(bootreason, BOOT_REASON_SEC_VIOLATION)) {
        log_msg("Security violation\n");
        return_bootstate("MALF");
    }

    if (argc  > 1  && !strcmp(argv[1], "-f")) {
        forcemode = 1;
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

        // Increment the counter but not on power on reset
        if (forcemode && (strcmp(bootreason,BOOT_REASON_POWER_ON_RESET) != 0))
        {
            increment_reboot_count();
        }

        return_bootstate(new_state);
    }
    if (forcemode) {
        clear_reboot_count();
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
            return_bootstate("USER");
        } else {
            return_bootstate(saved_state);
        }
    }

    if(!strcmp(bootreason, BOOT_REASON_POWER_KEY)) {
        log_msg("User pressed power button\n");
        return_bootstate("USER");
    }
    if(!strcmp(bootreason, BOOT_REASON_NSU)) {
        log_msg("software update (NSU)\n");
        return_bootstate("USER");
    }

    if(!strcmp(bootreason, BOOT_REASON_CHARGER) ||
       !strcmp(bootreason, BOOT_REASON_USB))
    {
        log_msg("User attached charger\n");
        return_bootstate("ACT_DEAD");
    }

    if(!strcmp(bootreason, BOOT_REASON_RTC_ALARM)) {
        log_msg("Alarm wakeup occured\n");
        return_bootstate("ACT_DEAD");
    }

    log_msg("Unknown bootreason '%s' passed by nolo\n", bootreason);
    return_bootstate("MALF");

    return 0; // never reached
}
