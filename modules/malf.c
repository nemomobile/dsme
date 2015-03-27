/**
   @file malf.c

   DSME MALF (malfunction) state handling
   <p>
   Copyright (C) 2009-2010 Nokia Corporation.

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

#ifndef __cplusplus
#define _GNU_SOURCE
#endif

#include "malf.h"
#include "runlevel.h"
#include "../include/dsme/modules.h"
#include "../include/dsme/logging.h"
#include "../include/dsme/modulebase.h"

#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>


static const char* const malf_reason_name[] = {
    "SOFTWARE",
    "HARDWARE",
    "SECURITY"
};

static const char* default_component = "(no component)";

static bool enter_malf(DSME_MALF_REASON reason,
                       const char*      component,
                       const char*      details)
{
    if (reason < 0 || reason >= DSME_MALF_REASON_COUNT) {
        reason = DSME_MALF_SOFTWARE;
    }

    dsme_log(LOG_INFO,
             "enter_malf '%s' '%s' '%s'",
             malf_reason_name[reason],
             component ? component : "(no component)",
             details ? details : "(no details)");

    int status = -1;
    pid_t pid;
    pid_t rc;
    char* args[] = {
        (char*)"enter_malf",
        (char*)malf_reason_name[reason],
        (char*)component,
        (char*)details,
        0
    };
    if ((pid = fork()) < 0) {
        dsme_log(LOG_CRIT, "fork failed, exiting");
        dsme_exit(EXIT_FAILURE);
        return false;
    } else if (pid == 0) {
        execv("/usr/sbin/enter_malf", args);

        dsme_log(LOG_CRIT, "entering MALF failed");
        return false;
    }

    while ((rc = wait(&status)) != pid)
        if (rc < 0 && errno == ECHILD)
            break;
    if (rc != pid || WEXITSTATUS(status) != 0) {
        dsme_log(LOG_CRIT, "enter_malf return value != 0, entering MALF failed");
        return false;
    }

    dsme_log(LOG_CRIT, "entering MALF state");
    return true;
}

DSME_HANDLER(DSM_MSGTYPE_ENTER_MALF, conn, malf)
{
    if (!enter_malf(malf->reason,
                    malf->component ? malf->component : default_component,
                    DSMEMSG_EXTRA(malf))) {
        /*
         * entering MALF failed; force shutdown by talking directly
         * to the init module (bypassing the state module)
         */
        DSM_MSGTYPE_SHUTDOWN msg = DSME_MSG_INIT(DSM_MSGTYPE_SHUTDOWN);
        msg.runlevel = DSME_RUNLEVEL_SHUTDOWN;

        broadcast_internally(&msg);
    }
}

module_fn_info_t message_handlers[] = {
    DSME_HANDLER_BINDING(DSM_MSGTYPE_ENTER_MALF),
    { 0 }
};

void module_init(module_t* module)
{
    dsme_log(LOG_DEBUG, "malf.so loaded");
}

void module_fini(void)
{
    dsme_log(LOG_DEBUG, "malf.so unloaded");
}
