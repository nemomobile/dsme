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

#ifndef __cplusplus
#define _GNU_SOURCE
#endif

#include <argz.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <ftw.h>
#include <limits.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/wait.h>

//#define DEBUG 1

#define ME "rpdir: "
#define MALLOC_ERR "could not allocate memory"

/* Smaller than that (bytes) files are not considered for deletion.
 * The gain would be insignificant, and small/zero-length files are often
 * used as flags, markers etc sort of stuff
 */
static const size_t LEAVE_SMALL_FILES_ALONE_LIMIT = (16 * 1024);

/* Time after last modification/access (seconds). */
static const int TIMEOUT = (60 * 30);

/* Max number of allowed open file descriptors in ftw(). */
static const size_t MAX_FTW_FDS = 100;

/* Skip checking the file modification time and size? */
static bool use_force = false;

struct entry {
    struct entry*   next;
    char*           fname;
};

static time_t curt;
static time_t checkpoint;
static char** open_files;
static size_t num_files;

static int string_cmp(const void *a, const void *b)
{
    const char **ia = (const char **)a;
    const char **ib = (const char **)b;
    return strcmp(*ia, *ib);
}

static char* make_command_new(char* dirs[])
{
    char* buf      = 0;
    char* cmdline  = 0;
    size_t len;

    if (argz_create(dirs, &cmdline, &len) == 0) {
        argz_stringify(cmdline, len, ' ');

        if (asprintf(&buf, "%s %s", "/usr/sbin/lsof -Fn" , cmdline) < 0) {
            goto out;
        }
    }

out:
    if (cmdline) {
        free(cmdline);
    }
    return buf;
}

static char** open_files_list_new(char* dirs[])
{
    char**                 files = 0;
    struct entry*          head  = 0;
    struct entry*          tail  = 0;
    size_t                 entries = 0;
    char                   line[PATH_MAX + 16];
    char*                  command = make_command_new(dirs);
    FILE*                  f = 0;

    if (!command) {
        goto out;
    }
    if (!(f = popen(command, "r"))) {
        goto out;
    }

    // line as follows:
    // p560
    // n/tmp/Xorg.0.log
    while (fgets(line, sizeof line, f)) {
        struct entry* e;

        /* Something else than a file: skip */
        if (*line != 'n') {
            continue;
        }

        line[strcspn(line, "\r\n")] = 0;

        e = calloc(1, sizeof *e);
        if (!e) {
            perror(ME MALLOC_ERR);
            break;
        }

        e->fname = strdup(&line[1]);
        if (!e->fname) {
            free(e);
            perror(ME MALLOC_ERR);
            break;
        }

        if (tail) {
            tail->next = e, tail = e;
        } else {
            tail = head = e;
        }
        entries++;
    }

    files = calloc(entries + 1, sizeof *files);
    size_t i = 0;
    struct entry* file;

    if (!files) {
        perror(ME MALLOC_ERR);
        exit(EXIT_FAILURE);
    }

    while ((file = head)) {
        head = file->next;
        files[i++] = file->fname;
        free(file);
    }
    files[i] = 0;
    num_files = i;

    if (i > 1) {
        qsort(files, num_files, sizeof *files, string_cmp);
    }

out:
    if (f) {
        pclose(f);
    }
    if (command) {
        free(command);
    }
    return files;
}

static bool is_open(const char* file)
{
    return bsearch(&file, open_files, num_files, sizeof *open_files, string_cmp);
}

static int reaper(const char *file, const struct stat64 *sb, int flag, struct FTW *ftwbuf)
{
    (void)ftwbuf;

    if (flag != FTW_F) {
        #ifdef DEBUG
            fprintf(stderr, "file '%s' is not a normal file, skipping\n", file);
        #endif
        goto out;
    }

    if (!use_force &&
        sb->st_size <= LEAVE_SMALL_FILES_ALONE_LIMIT) {
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
    if (use_force || (sb->st_mtime < checkpoint && sb->st_atime < checkpoint)) {
        if (is_open(file)) {
            #ifdef DEBUG
                fprintf(stderr, "file '%s' size %ld mod_age=%lus acc_age=%lus but still open, not deleting\n",
                        file, sb->st_size, curt-sb->st_mtime, curt-sb->st_atime);
            #endif
            goto out;
        }

        if (unlink(file) != 0) {
            fprintf(stderr, ME "failed to unlink file '%s': %m\n", file);
            goto out;
        }
        #ifdef DEBUG
        else fprintf(stderr, ME "deleted file '%s'\n", file);
        #endif

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
        if (nftw64(dirs[i], reaper, MAX_FTW_FDS, FTW_PHYS) != 0) {
            fprintf(stderr,  ME "ERROR traversing '%s': nftw64() failed: %m\n",
                    dirs[i]);
        }
    }

    return EXIT_SUCCESS;
}

static bool check_args(int argc, char* argv[])
{
    bool args_valid = true;

    if (use_force && argc < 3) {
        args_valid = false;
        goto out;
    }

    if (argc < 2) {
        args_valid = false;
    }

out:
    return args_valid;
}

int main(int argc, char* argv[])
{
    size_t arg_dir;

    if (argc > 1 && !strcmp(argv[1], "-f")) {
        use_force = true;
        arg_dir = 2;
    } else {
        arg_dir = 1;
    }

    if (!(check_args(argc, argv))) {
        fprintf(stderr, "Usage: " ME " [OPTION]... DIRECTORY...\n");
        fprintf(stderr, "Reap (remove) files in the given directories\n\n");
        fprintf(stderr, "-f\tremove files regardless of file modification time and date\n");
        return EXIT_FAILURE;
    }

    open_files = open_files_list_new(&argv[arg_dir]);

    return reap(&argv[arg_dir]);

    // NOTE: we leak open_files but we are going to exit.
}
