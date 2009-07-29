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

#include "dsme/modules.h"
#include "dsme/timers.h"
#include "dsme/logging.h"
#include <sys/mman.h>
#include <errno.h>
#include <string.h>


static bool kicking_enabled = false;

/* connection for the kicker process */
static endpoint_t* kicker_client = NULL;

/**
 * @ingroup hwwd
 * Timer for HW watchdog kicking.
 */
static dsme_timer_t hwwd_kick_timer = 0;

static void start_heartbeat_now(void);
static int  heartbeat(void* unused);


DSME_HANDLER(DSM_MSGTYPE_HWWD_KICK, client, msg)
{
    dsme_log(LOG_DEBUG, "hwwd_kick_force()");

    if (!kicking_enabled) {
        return;
    }

    start_heartbeat_now();
}

static void start_heartbeat_now(void)
{
        /* remove previous timer, if any */
	if (hwwd_kick_timer) {
		dsme_destroy_timer(hwwd_kick_timer);
		hwwd_kick_timer = 0;
	}

        /* do the first kick */
        (void)heartbeat(NULL);

	/* start kicking interval */
        hwwd_kick_timer = dsme_create_timer_high_priority(DSME_WD_PERIOD,
                                                          heartbeat,
                                                          NULL);
        if (hwwd_kick_timer) {
            dsme_log(LOG_NOTICE, "Setting WD timeout to %d", DSME_WD_PERIOD);
        } else {
            dsme_log(LOG_CRIT, "Unable to create a timeout for WD kicking");
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
 * This function kicks the HW watchdog and sends a heartbeat.
 */
static int heartbeat(void* unused)
{
    /* first kick the HW watchdog */
    if (kicking_enabled) {
        dsme_wd_kick();
    }

    /* then send the heartbeat */
    const DSM_MSGTYPE_HEARTBEAT beat = DSME_MSG_INIT(DSM_MSGTYPE_HEARTBEAT);
    broadcast_internally(&beat);
    dsme_log(LOG_DEBUG, "hwwd: heartbeat");

    return 1; /* keep the interval going */
}

/**
  @ingroup hwwd
  DSME messages handled by hwwd-module.
 */
module_fn_info_t message_handlers[] = {
    DSME_HANDLER_BINDING(DSM_MSGTYPE_HWWD_KICK),
    {0}
};

/**
 * Module initialization function. Starts HW watchdog
 */
void module_init(module_t *handle)
{
    if (!dsme_wd_init()) {
        dsme_log(LOG_ERR, "dsme_wd_init() failed, WD kicking disabled");
    } else {
        kicking_enabled = true;
    }
    start_heartbeat_now();

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
