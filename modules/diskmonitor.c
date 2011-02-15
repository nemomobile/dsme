/**
   @file diskmonitor.c
   Periodically monitors the disks and sends a message if the disk space usage
   exceeds the use limits.

   <p>
   Copyright (C) 2011 Nokia Corporation.

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

#include <iphbd/iphb_internal.h>

#include "diskmonitor.h"
#include "dsme/modules.h"
#include "dsme/logging.h"
#include "heartbeat.h"

#include <string.h>
#include <mntent.h>
#include <sys/statfs.h>

#define ArraySize(a) ((int)(sizeof(a)/sizeof(*a)))

typedef struct {
    const char*     mntpoint;
    unsigned short  max_usage_percent;
} disk_use_limit_t;

static disk_use_limit_t disk_space_use_limits[] = {
   /* [mount path, max usage percent] */
   {  "/tmp",                     95 },
   {  "/home/user/MyDocs",        95 }
};

static void schedule_next_wakeup(void)
{
    /* Don't cause too frequent wakeups for checking the disks;
     * the check is now twice in an hour.
     */

    DSM_MSGTYPE_WAIT msg = DSME_MSG_INIT(DSM_MSGTYPE_WAIT);
    msg.req.mintime = 1800; /* 30 minutes */
    msg.req.maxtime = msg.req.mintime + 120;
    msg.req.pid     = 0;
    msg.data        = 0;

    broadcast_internally(&msg);
}

static disk_use_limit_t* find_use_limit_for_mount(const char* mntpoint)
{
    disk_use_limit_t* use_limit = 0;
    int i;
    for (i=0; i < ArraySize(disk_space_use_limits); i++) {
        if (0 == strcmp(disk_space_use_limits[i].mntpoint, mntpoint)) {
            use_limit = &disk_space_use_limits[i];
            goto out;
        }
    }
out:
    return use_limit;
}

static void check_mount_use_limit(const char* mntpoint, disk_use_limit_t* use_limit)
{
    struct statfs s;
    long blocks_used;
    unsigned short blocks_percent_used;

    if (statfs(mntpoint, &s) != 0 || s.f_blocks <= 0) {
        dsme_log(LOG_WARNING, "failed to statfs the mount point (%s).", mntpoint);
        return;
    }

    blocks_used = s.f_blocks - s.f_bfree;
    blocks_percent_used = (unsigned short) (blocks_used * 100.f / (blocks_used + s.f_bavail) + 0.5f);

    if (blocks_percent_used >= use_limit->max_usage_percent) {
        dsme_log(LOG_WARNING, "disk space usage (%i percent used) for (%s) exceeded the limit (%i)",
                 blocks_percent_used, mntpoint, use_limit->max_usage_percent);

        DSM_MSGTYPE_DISK_SPACE msg = DSME_MSG_INIT(DSM_MSGTYPE_DISK_SPACE);
        msg.blocks_percent_used = blocks_percent_used;

        broadcast_internally_with_extra(&msg, strlen(mntpoint) + 1, mntpoint);
    }
}

static void check_disk_space_usage(void)
{
    disk_use_limit_t* use_limit;
    FILE* f = setmntent(_PATH_MOUNTED, "r");
    struct mntent m;
    char buf[1024];

    while (getmntent_r(f, &m, buf, sizeof(buf)) != 0) {
        use_limit = find_use_limit_for_mount(m.mnt_dir);

        if (!use_limit) {
            continue;
        }

        check_mount_use_limit(m.mnt_dir, use_limit);
    }
    endmntent(f);
}

DSME_HANDLER(DSM_MSGTYPE_WAKEUP, client, msg)
{
    check_disk_space_usage();

    schedule_next_wakeup();
}

module_fn_info_t message_handlers[] =
{
  DSME_HANDLER_BINDING(DSM_MSGTYPE_WAKEUP),
  { 0 }
};

void module_init(module_t* module)
{
    dsme_log(LOG_DEBUG, "diskmonitor.so loaded");

    schedule_next_wakeup();
}

void module_fini(void)
{
    dsme_log(LOG_DEBUG, "diskmonitor.so unloaded");
}
