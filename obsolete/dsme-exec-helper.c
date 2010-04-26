/**
   @file dsme-exec-helper.c

   This file implements an application that is supposed to be exec'd
   from dsme after forking. It does the non-async-signal-safe part
   of the child setup and then exec's the real target application.
   <p>
   Copyright (C) 2009-2010 Nokia Corporation.

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

#define _BSD_SOURCE

#include "dsme/oom.h"

#include <glib.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <sysexits.h>


static bool parse_args(int    argc,
                       char*  argv[],
                       uid_t* uid,
                       gid_t* gid,
                       int*   nice_val,
                       int*   oom_adj,
                       char** cmdline)
{
    bool parsed = false;

    if (argc == 6) {
        long uid_l;
        long gid_l;
        long nice_val_l;
        long oom_adj_l;

        if (sscanf(argv[1], "%li", &uid_l)      == 1 &&
            sscanf(argv[2], "%li", &gid_l)      == 1 &&
            sscanf(argv[3], "%li", &nice_val_l) == 1 &&
            sscanf(argv[4], "%li", &oom_adj_l)  == 1)
        {
            *uid      = uid_l;
            *gid      = gid_l;
            *nice_val = nice_val_l;
            *oom_adj  = oom_adj_l;
            *cmdline  = argv[5];

            parsed = true;
        }
    }

    return parsed;
}


static void non_async_signal_safe_child_setup(const char* cmdline,
                                              uid_t       uid,
                                              gid_t       gid,
                                              int         nice_val,
                                              int         oom_adj)
{
  if (nice_val != 0) {
      errno = 0;
      if (nice(nice_val) == -1 && errno != 0) {
          fprintf(stderr, "'%s' unable to nice to %i", cmdline, nice_val);
      }
  }
  if (oom_adj != 0) {
      if (!adjust_oom(oom_adj)) {
          fprintf(stderr,
                  "'%s' failed to set oom_adj; Unprotecting from OOM",
                  cmdline);
          if (!unprotect_from_oom()) {
              fprintf(stderr, "'%s' failed to unprotect from OOM", cmdline);
          }
      }
  } else if (nice_val >= 0) {
      if (!unprotect_from_oom()) {
          fprintf(stderr, "'%s' failed to unprotect from OOM", cmdline);
      }
  }

  struct passwd* passwd_field = getpwuid(uid);
  if (passwd_field != NULL) {
      if (initgroups(passwd_field->pw_name, passwd_field->pw_gid) == -1) {
          fprintf(stderr,
                  "'%s' initgroups() failed: %s",
                  cmdline,
                  strerror(errno));
          _exit(EX_NOPERM);
      }
  } else {
      fprintf(stderr,
              "'%s' requested UID (%d) not found, initgroups() not called",
              cmdline,
              uid);
  }

  if (setgid(gid)) {
      fprintf(stderr,
              "'%s' setgid(%d) => Permission denied",
              cmdline,
              gid);
      _exit(EX_NOPERM);
  }

  if (setuid(uid)) {
      fprintf(stderr,
              "'%s' setuid(%d) => Permission denied",
              cmdline,
              uid);
      _exit(EX_NOPERM);
  }
}


static bool make_argv(const char* cmdline, char*** argv, char** buf)
{
  *argv = NULL;
  *buf  = NULL;

  if (strpbrk(cmdline, "\"'~*?[](){}$;<>")) {
      *argv = (char**)malloc(sizeof(char*) * 4);
      if (!*argv) {
          goto cleanup;
      }

      (*argv)[0] = (char*)"/bin/sh";
      (*argv)[1] = (char*)"-c";
      (*argv)[2] = (char*)cmdline; /* TODO: a naughty const cast-away */
      (*argv)[3] = NULL;
  } else {
      /* This branch is not necessarily needed, but it "lighter" than
       * above version using "/bin/sh"
       */
      // TODO: replace this branch with a call to g_shell_parse_argv()
      int   argcount = 0;
      char* c;

      *buf = strdup(cmdline);
      if (!*buf) {
          goto cleanup;
      }

      *argv = (char**)malloc(sizeof(char *));
      if (!*argv) {
          goto cleanup;
      }
      (*argv)[argcount] = NULL;

      /* Parse command to argv */
      for (c = *buf; *c;) {
          char** tmp;

          /* Skip white space */
          /* Maybe isblank() is better? but it is gnu extension */
          if (isspace(*c)) {
              c++;
              continue;
          }

          (*argv)[argcount++] = c;
          tmp = (char**)realloc(*argv, sizeof(char*) * (argcount + 1));
          if (!tmp) {
              goto cleanup;
          }
          *argv = tmp;
          (*argv)[argcount] = NULL;

          /* find next blank */
          for (; *c; c++) {
              if (isspace(*c)) {
                  *c = '\0';
                  c++;
                  break;
              }
          }
      }
  }

  return true;

cleanup:
  free(*buf);
  *buf  = NULL;
  free(*argv);
  *argv = NULL;

  return false;
}

// TODO: log to syslog
// TODO: parameterize syslog or stderr on cmdline
int main(int argc, char* argv[])
{
    uid_t uid;
    gid_t gid;
    int   nice_val;
    int   oom_adj;
    char* cmdline;

    if (parse_args(argc, argv, &uid, &gid, &nice_val, &oom_adj, &cmdline)) {

        non_async_signal_safe_child_setup(cmdline, uid, gid, nice_val, oom_adj);

        char** args = 0;
        char*  buf  = 0;
        if (make_argv(cmdline, &args, &buf)) {

            fflush(0);
            execvp(args[0], args);

            fprintf(stderr,
                    "'%s' exec() failed: %s\n",
                    cmdline,
                    strerror(errno));

            free(buf);
            free(args);
        } else {
            fprintf(stderr, "'%s': building args failed\n", cmdline);
        }
    } else {
        fprintf(stderr, "%s: parsing args failed\n", argv[0]);
    }

    /* if we get here, it means the exec() failed */
    return EXIT_FAILURE;
}
