/**
   @file dsme_wd.c

   This file implements hardware watchdog kicker.
   <p>
   Copyright (C) 2004-2009 Nokia Corporation.

   @author Igor Stoppa <igor.stopaa@nokia.com>
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

#include "dsme_wd.h"
#include "dsme/logging.h"

#include <cal.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <sched.h>
#include <linux/types.h>
#include <linux/watchdog.h>


#define DSME_STATIC_STRLEN(s) (sizeof(s) - 1)


typedef struct wd_t {
    const char* file;   /* pathname of the watchdog device */
    int         period; /* watchdog timeout; 0 for keeping the default */
} wd_t;

static const wd_t wd[] = {
    { "/dev/twl4030_wdt", 30 }, /* set the twl wd timeout to 30 seconds */
    { "/dev/watchdog",    14 }  /* set the omap wd timeout to 14 seconds */
};

#define WD_COUNT (sizeof(wd) / sizeof(wd[0]))
static int wd_fd[WD_COUNT];

static bool  wd_enabled = true;


void dsme_wd_kick(void)
{
  if (wd_enabled) {
      int i;
      for (i = 0; i < WD_COUNT; ++i) {
          if (wd_fd[i] != -1 && write(wd_fd[i], "*", 1) == 1) {
#if 0 /* for debugging only */
              const char msg[] = "Kicked WD "

              (void)write(STDERR_FILENO, msg, DSME_STATIC_STRLEN(msg));
              (void)write(STDERR_FILENO, wd[i].file, strlen(wd[i].file));
              (void)write(STDERR_FILENO, "\n", 1);
#endif
          } else {
              const char msg[] = "Error kicking WD ";

              (void)write(STDERR_FILENO, msg, DSME_STATIC_STRLEN(msg));
              (void)write(STDERR_FILENO, wd[i].file, strlen(wd[i].file));
              (void)write(STDERR_FILENO, "\n", 1);

              /* must not kick later wd's if an earlier one fails */
              break;
          }
      }

#if 0 /* for debugging only */
      static struct timespec previous_timestamp = { 0, 0 };
      struct timespec timestamp;

      if (clock_gettime(CLOCK_MONOTONIC, &timestamp) != -1) {
          if (previous_timestamp.tv_sec != 0) {
              long ms;

              ms = (timestamp.tv_sec - previous_timestamp.tv_sec) * 1000;
              ms += (timestamp.tv_nsec - previous_timestamp.tv_nsec) / 1000000;

              if (ms > DSME_WD_PERIOD * 1000 + 100) {
                  fprintf(stderr, "took %ld ms between WD kicks\n", ms);
              }
          }
          previous_timestamp = timestamp;
      }
#endif
  }
}

static void read_cal_config(void)
{
    void*         vptr = NULL;
    unsigned long len  = 0;
    int           ret  = 0;
    char*         p;

    ret = cal_read_block(0, "r&d_mode", &vptr, &len, CAL_FLAG_USER);
    if (ret < 0) {
        dsme_log(LOG_ERR, "Error reading R&D mode flags, watchdogs enabled");
        return;
    }
    p = vptr;
    if (len >= 1 && *p) {
        dsme_log(LOG_DEBUG, "R&D mode enabled");

        if (len > 1) {
            if (strstr(p, "no-omap-wd")) {
                wd_enabled = false;
                dsme_log(LOG_NOTICE, "WD kicking disabled");
            } else {
                wd_enabled = true;
            }
        } else {
            wd_enabled = true;
            dsme_log(LOG_DEBUG, "No WD flags found, kicking enabled!");
        }
    }

    free(vptr);
    return;
}

bool dsme_wd_init(void)
{
    int i;

    for (i = 0; i < WD_COUNT; ++i) {
        wd_fd[i] = -1;
    }

    read_cal_config();
    if (!wd_enabled) {

        return false;

    } else {
        /* open watchdog devices */
        for (i = 0; i < WD_COUNT; ++i) {
            wd_fd[i] = open(wd[i].file, O_RDWR);
            if (wd_fd[i] == -1) {
                dsme_log(LOG_CRIT, "Error opening WD %s", wd[i].file);
                perror(wd[i].file);
            } else {
                if (wd[i].period != 0) {
                    dsme_log(LOG_NOTICE,
                             "Setting WD period to %d s for %s",
                             wd[i].period,
                             wd[i].file);
                    /* set the wd period */
                    /* ioctl() will overwrite tmp with the time left */
                    int tmp = wd[i].period;
                    if (ioctl(wd_fd[i], WDIOC_SETTIMEOUT, &tmp) != 0) {
                        dsme_log(LOG_CRIT,
                                 "Error setting WD period for %s",
                                 wd[i].file);
                    }
                } else {
                    dsme_log(LOG_NOTICE,
                             "Keeping default WD period for %s",
                             wd[i].file);
                }
            }
        }

        return true;
    }
}
