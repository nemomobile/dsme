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
#include "dsme/modules.h"
#include "dsme/logging.h"  /* for dsme_log()          */

#include <stdlib.h>        /* for exit()              */
#include <errno.h>         /* for errno               */
#include <unistd.h>        /* for fork() and execv()  */
#include <sys/wait.h>      /* for wait()              */

static bool enter_malf(int malf_id);

static bool enter_malf(int malf_id)
{
    int status = -1;
    pid_t pid;
    pid_t rc;
    char* args[] = {
        (char*)"enter_malf",
        (char*)DSME_MALF[malf_id].malf_type,
        (char*)DSME_MALF[malf_id].component,
        (char*)DSME_MALF[malf_id].reason,
        0
    };
    if ((pid = fork()) < 0) {
        dsme_log(LOG_CRIT, "fork failed, exiting");
        exit(EXIT_FAILURE);
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

DSME_HANDLER(DSM_MSGTYPE_ENTER_MALF, conn, msg)
{
    // TODO: we need the correct MALF reason
    if (!enter_malf(DSME_MALF_REBOOTLOOP)) {
        /* Shutdown because entering MALF failed */
        DSM_MSGTYPE_SHUTDOWN_REQ req = DSME_MSG_INIT(DSM_MSGTYPE_SHUTDOWN_REQ);

        broadcast_internally(&req);
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
