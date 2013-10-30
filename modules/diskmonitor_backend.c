/**
   @file diskmonitor_backend.c

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

#include "diskmonitor_backend.h"
#include "diskmonitor.h"

#include "dsme/modules.h"
#include "dsme/logging.h"

#include <string.h>
#include <mntent.h>
#include <sys/statfs.h>
#include <stdbool.h>

#define ArraySize(a) (sizeof(a)/sizeof*(a))

typedef struct {
    const char*     mntpoint;
    int             max_usage_percent;
} disk_use_limit_t;

static disk_use_limit_t disk_space_use_limits[] = {
   /* [mount path, max usage percent] */
   {  "/",                        90 },
   {  "/tmp",                     70 },
   {  "/run",                     70 },
   {  "/home",                    90 }
};

static disk_use_limit_t* find_use_limit_for_mount(const char* mntpoint)
{
    disk_use_limit_t* use_limit = 0;
    size_t i;

    for (i=0; i < ArraySize(disk_space_use_limits); i++) {
        if (0 == strcmp(disk_space_use_limits[i].mntpoint, mntpoint)) {
            use_limit = &disk_space_use_limits[i];
            goto out;
        }
    }
out:
    return use_limit;
}

static bool check_mount_use_limit(const char* mntpoint, disk_use_limit_t* use_limit)
{
    struct statfs s;
    int blocks_percent_used;
    bool over_limit = false;

    memset(&s, 0, sizeof(s));

    if (statfs(mntpoint, &s) != 0 || s.f_blocks <= 0) {
        dsme_log(LOG_WARNING, "diskmonitor: failed to statfs the mount point (%s).", mntpoint);
        return false;
    }

    blocks_percent_used = (int)((s.f_blocks - s.f_bfree) * 100.f / s.f_blocks + 0.5f);

    if (blocks_percent_used >= use_limit->max_usage_percent) {
        dsme_log(LOG_WARNING, "diskmonitor: disk space usage (%i percent used) for (%s) exceeded the limit (%i)",
                 blocks_percent_used, mntpoint, use_limit->max_usage_percent);

        DSM_MSGTYPE_DISK_SPACE msg = DSME_MSG_INIT(DSM_MSGTYPE_DISK_SPACE);
        msg.blocks_percent_used = blocks_percent_used;

        broadcast_internally_with_extra(&msg, strlen(mntpoint) + 1, mntpoint);
        over_limit = true;
    } 
    return over_limit;
}

void check_disk_space_usage(void)
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

        if (check_mount_use_limit(m.mnt_dir, use_limit)) {
            /* When we find first disk_usage over the limit, no need to continue 
             *  Warning has been given and tempreaper has been started
             */
            break;
        }
    }
    endmntent(f);
}
