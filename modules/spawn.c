/**
   @file spawn.c

   This file implements process spawning and child exit signal catching.
   It also sends notifications on child exits.
   <p>
   Copyright (C) 2004-2010 Nokia Corporation.

   @author Ari Saastamoinen
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

#include "spawn.h"
#include "dsme/logging.h"
#include "dsme/modules.h"
#include "dsme/messages.h"
#include "dsme/oom.h"

#include <glib.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <pwd.h>
#include <stdbool.h>
#include <sched.h>


#define DSME_STATIC_STRLEN(s) (sizeof(s) - 1)

// TODO: is /sbin/ the right place for the exec helper?
#define DSME_EXEC_HELPER_PATH "/sbin/dsme-exec-helper"


static void announce_child_exit(GPid pid, gint status, gpointer unused);


/* WARNING: non-re-entrant due to static non-const strings */
static bool make_argv_for_exec_helper(const char* cmdline,
                                      uid_t       uid,
                                      gid_t       gid,
                                      int         nice_val,
                                      int         oom_adj,
                                      char***     argv)
{
    bool made = false;

    *argv = NULL;

    *argv = malloc(7 * sizeof(char*));
    if (*argv) {
        // static strings, long enough for strings reprentations
        static char uid_string[25];
        static char gid_string[25];
        static char nice_val_string[25];
        static char oom_adj_string[25];

        sprintf(uid_string,      "%li", (long)uid);
        sprintf(gid_string,      "%li", (long)gid);
        sprintf(nice_val_string, "%li", (long)nice_val);
        sprintf(oom_adj_string,  "%li", (long)oom_adj);

        (*argv)[0] = (char*)DSME_EXEC_HELPER_PATH;
        (*argv)[1] = uid_string;
        (*argv)[2] = gid_string;
        (*argv)[3] = nice_val_string;
        (*argv)[4] = oom_adj_string;
        (*argv)[5] = (char*)cmdline; /* TODO: a naughty const cast-away */
        (*argv)[6] = NULL;

        made = true;
    }

    return made;
}

/*
 * this function is called between fork() and exec(),
 * so it must only do async-signal-safe operations
 */
static void async_signal_safe_child_setup(const char* cmdline)
{
  int dummy;

  /* restore the default scheduler */
  struct sched_param sch;
  memset(&sch, 0, sizeof(sch));
  sch.sched_priority = 0;
  if (sched_setscheduler(0, SCHED_OTHER, &sch) == -1) {
      const char msg[] = "unable to set the scheduler: ";

      dummy = write(STDERR_FILENO, msg, DSME_STATIC_STRLEN(msg));
      dummy = write(STDERR_FILENO, cmdline, strlen(cmdline));
      dummy = write(STDERR_FILENO, "\n", 1);
  }

  /* set the priority first to zero as dsme runs with -1,
   * then to the requested
   */
  if (setpriority(PRIO_PROCESS, 0, 0) != 0) {
      const char msg[] = "unable to set the priority to 0: ";

      dummy = write(STDERR_FILENO, msg, DSME_STATIC_STRLEN(msg));
      dummy = write(STDERR_FILENO, cmdline, strlen(cmdline));
      dummy = write(STDERR_FILENO, "\n", 1);
  }

  int i;
  int max_fd_count;

  /* close any extra descriptors */
  max_fd_count = getdtablesize();
  if (max_fd_count == -1)  {
      max_fd_count = 256;
  }
  for (i = 3; i < max_fd_count; ++i) {
      (void)close(i);
  }

  /* establish a new session */
  if (setsid() < 0) {
      const char msg[] = "setsid() failed: ";

      dummy = write(STDERR_FILENO, msg, DSME_STATIC_STRLEN(msg));
      dummy = write(STDERR_FILENO, cmdline, strlen(cmdline));
      dummy = write(STDERR_FILENO, "\n", 1);
  }
}


pid_t spawn_proc(const char* cmdline,
                 uid_t       uid,
                 gid_t       gid,
                 int         nice_val,
                 int         oom_adj,
                 char*       env[])
{
  int         retval  = 0;
  char**      args    = NULL;
  pid_t       pid;


  /*
   * For multi-threaded processes (such as dsme), a child may only
   * execute async-signal-safe operations between fork() and exec().
   * Since dsme's children need some setup that is not async-signal-safe,
   * we first exec a helper application. The helper application
   * can then do the non-async-signal-safe operations before
   * execing the target application.
   */
  if (make_argv_for_exec_helper(cmdline, uid, gid, nice_val, oom_adj, &args)) {

      fflush(0);
      pid = fork();

      if (pid == 0) {
          /* child */

          async_signal_safe_child_setup(cmdline);

          if (env) {
              execve(args[0], args, env);
          } else {
              execvp(args[0], args);
          }

          fprintf(stderr, "'%s' exec() failed: %s\n", cmdline, strerror(errno));
          _exit(EXIT_FAILURE);

      } else if (pid == -1) {
          /* error */
          dsme_log(LOG_CRIT, "fork() failed: %s", strerror(errno));
      } else {
          /* parent */
          g_child_watch_add(pid, announce_child_exit, announce_child_exit);
          retval = pid;
      }

      free(args);
  } else {
      dsme_log(LOG_CRIT, "error parsing cmdline: '%s'", cmdline);
  }

  return retval;
}


static void announce_child_exit(GPid pid, gint status, gpointer unused)
{
  DSM_MSGTYPE_PROCESS_EXITED msg =
    DSME_MSG_INIT(DSM_MSGTYPE_PROCESS_EXITED);

  msg.pid    = pid;
  msg.status = status;

  broadcast_internally(&msg);

  g_spawn_close_pid(pid);
}


void spawn_shutdown(void) {
  // TODO: no guarantee that announce_child_exit is a unique value :(
  while (g_source_remove_by_user_data(announce_child_exit)) {
      // EMPTY LOOP
  }
}
