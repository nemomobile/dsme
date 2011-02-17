/**
   @file rpdir.c

   Reaps (removes) all non-open files from a directory.
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

#include <errno.h>
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

//#define DEBUG 1

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

static time_t curt;
static time_t checkpoint;

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
    if (flag != FTW_F) {
        #ifdef DEBUG
            fprintf(stderr, "file '%s' is not a normal file, skipping\n", file);
        #endif
        goto out;
    }

    if (!(sb->st_size > LEAVE_SMALL_FILES_ALONE_LIMIT)) {
        #ifdef DEBUG
            fprintf(stderr, "file '%s' size %ld too small, skipping\n", file, sb->st_size);
        #endif
        goto out;
    }

    /*
     * All regular files which were not changed the last TIMEOUT seconds.
     * instead of finding age of file, we check is mtime/atime
     * older than checkpoint which we already have set to time in the past
     */
    if ((sb->st_mtime < checkpoint) && (sb->st_atime < checkpoint)) {
        if (is_open(file)) {
            #ifdef DEBUG
                fprintf(stderr, "file '%s' size %ld mod_age=%lus acc_age=%lus but still open, not deleting",
                        file, sb->st_size, curt-sb->st_mtime, curt-sb->st_atime);
            #endif
            goto out;
        }

        if (unlink(file) != 0) {
            #ifdef DEBUG
                fprintf(stderr, "failed to unlink file '%s', %s", file, strerror(errno));
            #endif
            goto out;
        }
    } else {
        #ifdef DEBUG
            fprintf(stderr, "file '%s' modified too recently mod_age=%lus acc_age=%lus, skipping\n",
                    file, curt-sb->st_mtime, curt-sb->st_atime);
        #endif
    }

out:
    return 0;
}

static int reap(char* dirs[])
{
    size_t i;

    time(&curt);
    checkpoint = curt - TIMEOUT;

    for (i = 0; dirs[i]; i++) {
        ftw(dirs[i], reaper, MAX_FTW_FDS);
    }

    return EXIT_SUCCESS;
}

int main(int argc, char* argv[])
{
    if (argc == 1) {
        fprintf(stderr, "Usage: rpdir <directories>\n");
        return EXIT_FAILURE;
    }

    return reap(&argv[1]);
}
