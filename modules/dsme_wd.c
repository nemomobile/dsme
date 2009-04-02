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

#define IDLE_PRIORITY 0

static const char wd_file[] = "/dev/watchdog";
static const int  wd_period = 30;

static int wd_enabled =  1;
static int wd_fd      = -1;

static sem_t dsme_wd_sem;


void dsme_wd_kick(void)
{
    sem_post(&dsme_wd_sem);
    dsme_log(LOG_DEBUG, "Got a permission to kick watchdog...");
}

static void* dsme_wd_loop(void* param)
{
    /*
     * This is not portable because it relies on the linux way of
     * implementing threads as different processes. On a different system
     * they could have the same PID of the father that spawned them.
     */
    setpriority(PRIO_PROCESS, 0, IDLE_PRIORITY);

    while(1) {
        sem_wait(&dsme_wd_sem);

        if (wd_enabled) {
            if (write(wd_fd, "*\n", 2) != 2)
                dsme_log(LOG_CRIT, "error kicking watchdog!");
            else
                dsme_log(LOG_DEBUG, "WD kicked!");
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
        dsme_log(LOG_ERR, "Error reading R&D mode flags, watchdog enabled");
        return;
    }
    p = vptr;
    if (len >= 1 && *p) {
        dsme_log(LOG_DEBUG, "R&D mode enabled");

        if (len > 1) {
            if (strstr(p, "no-omap-wd")) {
                wd_enabled = 0;
                dsme_log(LOG_NOTICE, "WD kicking disabled");
            } else {
                wd_enabled = 1;
            }
        } else {
            wd_enabled = 1;
            dsme_log(LOG_DEBUG, "No WD flags found, kicking enabled!");
        }
    }

    free(vptr);
    return;
}

int dsme_init_wd(void)
{
    pthread_attr_t tattr;
    pthread_t tid;
    int tmp;
    int ret;
    struct sched_param param;

    ret = sem_init(&dsme_wd_sem, 0, 0);
    if (ret != 0) {
        dsme_log(LOG_CRIT, "Error initialising semaphore");
        return ret;
    }

    read_cal_config();
    if (!wd_enabled)
        return -1;

    if (wd_enabled) {
        wd_fd = open(wd_file, O_RDWR);
        if (wd_fd == -1) {
            dsme_log(LOG_CRIT, "Error opening the watchdog device");
            perror(wd_file);
            return errno;
        }

        /* tmp will be loaded by the ioctl with the time left */
        tmp = wd_period;
        ret = ioctl(wd_fd, WDIOC_SETTIMEOUT, &tmp);
        if (ret != 0) {
            dsme_log(LOG_CRIT, "Error initialising watchdog");
            return ret;
        }
    }

    ret = pthread_attr_init (&tattr);
    if (ret != 0) {
        dsme_log(LOG_CRIT, "Error getting thread attributes");
        return ret;
    }

    ret = pthread_attr_getschedparam (&tattr, &param);
    if (ret != 0) {
        dsme_log(LOG_CRIT, "Error getting scheduling parameters");
        return ret;
    }

    ret = pthread_create (&tid, &tattr, dsme_wd_loop, NULL);
    if (ret != 0) {
        dsme_log(LOG_CRIT, "Error creating new thread");
        return ret;
    }

    return ret;
}
