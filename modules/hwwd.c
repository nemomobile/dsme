/**
   @file hwwd.c

   This file implements hardware watchdog kicker.
   The kicking is done in another, low-priority thread.
   <p>
   Copyright (C) 2004-2010 Nokia Corporation.

   @author Ismo Laitinen <ismo.laitinen@nokia.com>
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
#include "heartbeat.h"

#include "dsme/modules.h"
#include "dsme/timers.h"
#include "dsme/logging.h"
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>


#define DSME_STATIC_STRLEN(s) (sizeof(s) - 1)


void dsme_handle_unresponsive_main_thread(void);


static bool              kicking_enabled             = false;
static volatile unsigned pending_heartbeat_msg_count = 0;


/*
 * This function is called just before the WD thread goes to sleep
 */
static void kick_hwwd(void)
{
    if (kicking_enabled) {
        dsme_wd_kick();
    }
}

/*
 * This function is called just after the WD thread wakes up
 */
static bool heartbeat(void)
{
    /* first kick the HW watchdog */
    kick_hwwd();

    /* then see if the mainloop is running */
    if (pending_heartbeat_msg_count++ >= 5) { // TODO: give the 5 some nice name
        dsme_handle_unresponsive_main_thread();
    }

    return true;
}

void dsme_handle_unresponsive_main_thread(void)
{
    const char msg[] = 
        "dsme: mainloop frozen or seriously lagging; aborting\n";
    int dummy = write(STDERR_FILENO, msg, DSME_STATIC_STRLEN(msg));
    (void)dummy;
    abort();
}

DSME_HANDLER(DSM_MSGTYPE_HEARTBEAT, client, msg)
{
    /* indicate that the main loop is running */
    pending_heartbeat_msg_count = 0;
    dsme_log(LOG_DEBUG, "main loop is alive");
}

DSME_HANDLER(DSM_MSGTYPE_HWWD_KICK, client, msg)
{
    dsme_log(LOG_DEBUG, "forced hwwd kick");

    kick_hwwd();
}


/**
  @ingroup hwwd
  DSME messages handled by hwwd-module.
 */
module_fn_info_t message_handlers[] = {
    DSME_HANDLER_BINDING(DSM_MSGTYPE_HEARTBEAT),
    DSME_HANDLER_BINDING(DSM_MSGTYPE_HWWD_KICK),
    {0}
};

/**
 * Module initialization function. Starts HW watchdog
 */
void module_init(module_t *handle)
{
    if (!dsme_wd_init()) {
        dsme_log(LOG_ERR, "no WD's opened; WD kicking disabled");
    } else {
        kicking_enabled = true;
    }

    heartbeat();

    DSM_MSGTYPE_HEARTBEAT_START msg =
        DSME_MSG_INIT(DSM_MSGTYPE_HEARTBEAT_START);
    msg.presleep_cb               = kick_hwwd;
    msg.sleep_interval_in_seconds = DSME_WD_PERIOD;
    msg.postsleep_cb              = heartbeat;
    broadcast_internally(&msg);

    dsme_log(LOG_DEBUG, "libhwwd.so loaded");
}

void module_fini(void)
{
    dsme_log(LOG_DEBUG, "libhwwd.so unloaded");
}
