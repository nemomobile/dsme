/**
   @file dsme_wd.c

   This file implements hardware watchdog kicker thread.
   The kicking is done in another, low-priority thread.
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

#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <linux/types.h>
#include <linux/watchdog.h>

#define DSME_WD_PRIORITY  (-20)
#define DSME_WD_SCHEDULER SCHED_RR

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
static sem_t dsme_wd_sem;


void dsme_wd_kick(void)
{
    sem_post(&dsme_wd_sem);
    dsme_log(LOG_DEBUG, "Got a permission to kick watchdogs...");
}

static void* dsme_wd_loop(void* param)
{
    /*
     * This is not portable because it relies on the linux way of
     * implementing threads as different processes. On a different system
     * they could have the same PID of the father that spawned them.
     */
    dsme_log(LOG_NOTICE, "setting priority %d", DSME_WD_PRIORITY);
    if (setpriority(PRIO_PROCESS, 0, DSME_WD_PRIORITY) == -1) {
        dsme_log(LOG_CRIT, "setpriority(): %s", strerror(errno));
    }

    dsme_log(LOG_NOTICE, "setting scheduler %d", DSME_WD_SCHEDULER);
    struct sched_param sch;
    memset(&sch, 0, sizeof(sch));
    sch.sched_priority = sched_get_priority_max(DSME_WD_SCHEDULER);
    if (sched_setscheduler(0, DSME_WD_SCHEDULER, &sch) == -1) {
        dsme_log(LOG_CRIT, "sched_get_priority_min(): %s", strerror(errno));
    }


    while (true) {
        sem_wait(&dsme_wd_sem);

        if (wd_enabled) {
            int i;
            for (i = 0; i < WD_COUNT; ++i) {
                if (wd_fd[i] != -1 && write(wd_fd[i], "*", 1) == 1) {
                    dsme_log(LOG_DEBUG, "Kicked WD %s", wd[i].file);
                } else {
                    dsme_log(LOG_CRIT, "Error kicking WD %s", wd[i].file);
                    /* must not kick later wd's if an earlier one fails */
                    break;
                }
            }
        }
    }
    return 0;
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
    pthread_attr_t tattr;
    pthread_t tid;
    int i;
    struct sched_param param;

    for (i = 0; i < WD_COUNT; ++i) {
        wd_fd[i] = -1;
    }

    if (sem_init(&dsme_wd_sem, 0, 0) != 0) {
        dsme_log(LOG_CRIT, "Error initialising semaphore");
        return false;
    }

    read_cal_config();
    if (!wd_enabled) {
        return false;
    }

    if (wd_enabled) {
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
    }

    if (pthread_attr_init (&tattr) != 0) {
        dsme_log(LOG_CRIT, "Error getting thread attributes");
        return false;
    }

    if (pthread_attr_getschedparam (&tattr, &param) != 0) {
        dsme_log(LOG_CRIT, "Error getting scheduling parameters");
        return false;
    }

    if (pthread_create (&tid, &tattr, dsme_wd_loop, NULL) != 0) {
        dsme_log(LOG_CRIT, "Error creating new thread");
        return false;
    }

    return true;
}
