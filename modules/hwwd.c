/**
   @file hwwd.c

   This file implements hardware watchdog kicker.
   The kicking is done in another, low-priority thread.
   <p>
   Copyright (C) 2004-2009 Nokia Corporation.

   @author Ismo Laitinen <ismo.laitinen@nokia.com>

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


/**
 * @defgroup modules DSME Modules
 */

/**
 * @defgroup hwwd Hardware watchdog 
 * @ingroup modules
 *
 */

#include "hwwd.h"
#include "dsme_wd.h"
#ifdef DSME_WD_SYNC
#  include "processwd.h"
#endif

#include "dsme/modules.h"
#include "dsme/timers.h"
#include "dsme/logging.h"
#include <sys/mman.h>
#include <errno.h>
#include <string.h>

/* Period for kicking; i.e. how often dsme wakes up to kick the watchdogs */
#define DSME_WD_PERIOD 5 /* seconds */

typedef enum {
  KICKER_TYPE_NONE,
  KICKER_TYPE_THREAD,
  KICKER_TYPE_CLIENT
} KICKER_TYPE;

static KICKER_TYPE kicker_type = KICKER_TYPE_NONE;

/* connection for the kicker process */
static endpoint_t* kicker_client = NULL;

/**
 * @ingroup hwwd
 * Timer for HW watchdog kicking.
 */
static dsme_timer_t hwwd_kick_timer = 0;

static void start_kicking_now(void);
static int hwwd_kick_fn(void* unused);


DSME_HANDLER(DSM_MSGTYPE_HWWD_KICKER, client, msg)
{
	if (kicker_type != KICKER_TYPE_NONE) {
		dsme_log(LOG_ERR, "libhwwd: kicker already registered!");
		return;
	}

	kicker_client   = endpoint_copy(client);
	kicker_type     = KICKER_TYPE_CLIENT;
	start_kicking_now();

	dsme_log(LOG_INFO, "libhwwd: kicker registered and kicked");
}

DSME_HANDLER(DSM_MSGTYPE_HWWD_KICK, client, msg)
{
	dsme_log(LOG_DEBUG, "hwwd_kick_force()");
	
	if (kicker_type == KICKER_TYPE_NONE) {
		return;
        }
	
	start_kicking_now();
}

static void start_kicking_now(void)
{
        /* remove previous timer, if any */
	if (hwwd_kick_timer) {
		dsme_destroy_timer(hwwd_kick_timer);
		hwwd_kick_timer = 0;
	}

        /* do the first kick */
        (void)hwwd_kick_fn(NULL);

	/* start kicking interval */
        hwwd_kick_timer = dsme_create_timer_high_priority(DSME_WD_PERIOD,
                                                          hwwd_kick_fn,
                                                          NULL);
        if (!hwwd_kick_timer) {
            dsme_log(LOG_CRIT, "Unable to create a timer for WD kicking.., expect reset..");
        }

        static int locked = -1;
        if (locked == -1) {
            dsme_log(LOG_NOTICE, "locking to RAM");
            if ((locked = mlockall(MCL_CURRENT|MCL_FUTURE)) == -1) {
                dsme_log(LOG_CRIT, "mlockall(): %s", strerror(errno));
            }
        }
}


/**
 * This function kicks the HW watchdog.
 */
static int hwwd_kick_fn(void* unused)
{
	/* Let the kicker process or thread kick */
        switch (kicker_type) {

        case KICKER_TYPE_CLIENT:
          if (kicker_client) {
              DSM_MSGTYPE_HWWD_KICK msg = DSME_MSG_INIT(DSM_MSGTYPE_HWWD_KICK);
              endpoint_send(kicker_client, &msg);
          }
          break;

        case KICKER_TYPE_THREAD:
            dsme_wd_kick();
            break;

        default:
            /* do nothing */
            break;
        }

#ifdef DSME_WD_SYNC
        {
	/* Kick ProcessWD */
	const DSM_MSGTYPE_PROCESSWD_MANUAL_PING ping =
          DSME_MSG_INIT(DSM_MSGTYPE_PROCESSWD_MANUAL_PING);
	broadcast_internally(&ping);
	dsme_log(LOG_DEBUG, "hwwd: Manual processwd ping requested");
        }
#endif

        return 1; /* keep the interval going */
}

/**
  @ingroup hwwd
  DSME messages handled by hwwd-module.
 */
module_fn_info_t message_handlers[] = {
    DSME_HANDLER_BINDING(DSM_MSGTYPE_HWWD_KICK),
    DSME_HANDLER_BINDING(DSM_MSGTYPE_HWWD_KICKER),
    {0}
};

/**
 * Module initialization function. Starts HW watchdog
 */
void module_init(module_t *handle)
{
        if (!dsme_wd_init()) {
          dsme_log(LOG_ERR, "dsme_wd_init() failed, WD kicking disabled");
	  dsme_log(LOG_DEBUG, "libhwwd.so: waiting for the kicker to register");
        } else {
          kicker_type = KICKER_TYPE_THREAD;
#ifndef DSME_WD_SYNC
          start_kicking_now();
#endif
        }
#ifdef DSME_WD_SYNC
        start_kicking_now();
#endif

	dsme_log(LOG_DEBUG, "libhwwd.so loaded");
}

void module_fini(void)
{
        if (kicker_client) {
          endpoint_free(kicker_client);
          kicker_client = 0;
        }

	dsme_log(LOG_DEBUG, "libhwwd.so unloaded");
}
