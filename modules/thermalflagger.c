/**
   @file thermalflagger.c

   Create a file to flag overheat situation.
   This is needed because there are cases where a shutdown
   might be converted to a reboot. If the overheat flag file
   is present, a shutdown must be considered a thermal shutdown
   and must not be converted to a reboot.
   <p>
   Copyright (C) 2011 Nokia Corporation.

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

#ifndef __cplusplus
#define _GNU_SOURCE
#endif

#include "dsme/modules.h"
#include "dsme/logging.h"
#include <dsme/state.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>


#define DSME_THERMAL_FLAG_FILE "/var/lib/dsme/force_shutdown"


static void write_thermal_flag(void)
{
    int i = open(DSME_THERMAL_FLAG_FILE, O_WRONLY | O_CREAT, 0644);
    if (i == -1 || fsync(i) == -1 || close(i) == -1) {
        dsme_log(LOG_ERR,
                 "Error creating %s: %s",
                 DSME_THERMAL_FLAG_FILE,
                 strerror(errno));
    }
}

static void remove_thermal_flag(void)
{
    if (unlink(DSME_THERMAL_FLAG_FILE) == -1 && errno != ENOENT) {
        dsme_log(LOG_WARNING,
                 "Error removing %s: %s",
                 DSME_THERMAL_FLAG_FILE,
                 strerror(errno));
    }
}

DSME_HANDLER(DSM_MSGTYPE_SET_THERMAL_STATUS, client, thermal_state)
{
    if (thermal_state->status == DSM_THERMAL_STATUS_OVERHEATED) {
        write_thermal_flag();
    } else {
        /* don't remove DSME_THERMAL_FLAG_FILE */
    }
}

module_fn_info_t message_handlers[] = {
  DSME_HANDLER_BINDING(DSM_MSGTYPE_SET_THERMAL_STATUS),
  { 0 }
};

void module_init(module_t* handle)
{
  dsme_log(LOG_DEBUG, "thermalflagger.so loaded");

  remove_thermal_flag();
}

void module_fini(void)
{
  dsme_log(LOG_DEBUG, "thermalflagger.so unloaded");
}
