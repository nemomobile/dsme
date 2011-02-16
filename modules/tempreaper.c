/**
   @file tempreaper.c

   DSME module to clean up orphaned temporary files.
   <p>
   Copyright (C) 2011 Nokia Corporation.

   @author Guillem Jover <guillem.jover@nokia.com>
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

#include "diskmonitor.h"
#include "dsme/modules.h"
#include "dsme/logging.h"

#include <errno.h>
#include <glib.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <ftw.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/wait.h>

/* The tempdirs we will cleanup.
 */
static const char* const tempdirs[] = {
    "/var/tmp",
    "/var/log",
    NULL
};

/* Smaller than that (bytes) files are not considered for deletion.
 * The gain would be insignificant, and small/zero-length files are often
 * used as flags, markers etc sort of stuff
 */
static const size_t LEAVE_SMALL_FILES_ALONE_LIMIT = (16 * 1024);

/* Time after last modification/access (seconds). */
static const int TIMEOUT = (60 * 30);

/* Max number of allowed open file descriptors in ftw(). */
static const size_t MAX_FTW_FDS = 100;

/* Use a hardcoded path to speedup execl.  */
static const char* PATH_LSOF = "/usr/bin/lsof";

/* If this many blocks are in use, trigger cleaning */
static const unsigned short MAX_USED_BLOCK_PERCENTAGE = 95;

static time_t curt;
static time_t checkpoint;
static pid_t reaper_pid = -1;

static bool is_open(const char* file)
{
    int status;
    pid_t pid;

    pid = fork();
    switch (pid) {
    case -1:
        return false;
    case 0:
        execl(PATH_LSOF, "lsof", file, NULL);
        _exit(1);
    default:
        waitpid(pid, &status, 0);

        /* Is this file not open at the moment? */
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            return true;
        } else {
            return false;
        }
    }
}

static int reaper(const char *file, const struct stat *sb, int flag)
{
    /* All regular files which were not changed the last TIMEOUT seconds
     * and bigger than "leave small ones alone" limit
     */
    if (flag == FTW_F) {
        if (sb->st_size > LEAVE_SMALL_FILES_ALONE_LIMIT) {
            /*
             * instead of finding age of file, we check is mtime/atime
             * older than checkpoint which we already have set to time in the past
             */
            if ((sb->st_mtime < checkpoint) && (sb->st_atime < checkpoint)) {
                if (is_open(file)) {
                    dsme_log(LOG_DEBUG, "file '%s' size %ld mod_age=%lus acc_age=%lus but still open, not deleting",
                            file, sb->st_size, curt-sb->st_mtime, curt-sb->st_atime);
                } else {
                    dsme_log(LOG_DEBUG, "file '%s' size %ld mod_age=%lus acc_age=%lus, deleting",
                          file, sb->st_size, curt-sb->st_mtime, curt-sb->st_atime);
                    unlink(file);
                }
            }
        }
    }
    return 0;
}

static void delete_orphaned_files(void)
{
    int i;

    time(&curt);
    checkpoint = curt - TIMEOUT;

    /* Cleanup tempdirs */
    for (i = 0; tempdirs[i]; i++) {
        ftw(tempdirs[i], reaper, MAX_FTW_FDS);
    }
}

static pid_t reaper_process_new(void)
{
    pid_t pid = fork();

    if (pid == 0) {
        /* Child; set a reasonably low priority, DSME runs as the highest priority process (-1)
           so we don't want to use the inherited priority */
        if (setpriority(PRIO_PROCESS, 0, 8) != 0) {
            dsme_log(LOG_CRIT, "setpriority() failed: %s. Exit.", strerror(errno));
            _exit(EXIT_FAILURE);
        }

        delete_orphaned_files();
        _exit(EXIT_SUCCESS);
    } else if (pid == -1) {
        /* error */
        dsme_log(LOG_CRIT, "fork() failed: %s", strerror(errno));
    } else {
        /* parent */
    }
    return pid;
}

static void temp_reaper_finished(GPid pid, gint status, gpointer unused)
{
    reaper_pid = -1;
}

static bool disk_space_running_out(const DSM_MSGTYPE_DISK_SPACE* msg)
{
    const char *mount_path = DSMEMSG_EXTRA(msg);
    return (msg->blocks_percent_used >= MAX_USED_BLOCK_PERCENTAGE) &&
           (strcmp(mount_path, "/tmp") == 0 || strcmp(mount_path, "/") == 0);
}

DSME_HANDLER(DSM_MSGTYPE_DISK_SPACE, conn, msg)
{
    if (reaper_pid != -1) {
        return;
    }

    if (disk_space_running_out(msg)) {
        reaper_pid = reaper_process_new();

        if (reaper_pid != -1) {
            g_child_watch_add(reaper_pid, temp_reaper_finished, temp_reaper_finished);
        }
    }
}

module_fn_info_t message_handlers[] = {
    DSME_HANDLER_BINDING(DSM_MSGTYPE_DISK_SPACE),
    { 0 }
};

void module_init(module_t* module)
{
    dsme_log(LOG_DEBUG, "tempreaper.so loaded");
}

void module_fini(void)
{
    if (reaper_pid != -1) {
        dsme_log(LOG_INFO, "killing temp reaper with pid %i", reaper_pid);
        kill(reaper_pid, SIGKILL);
    }

    dsme_log(LOG_DEBUG, "tempreaper.so unloaded");
}
