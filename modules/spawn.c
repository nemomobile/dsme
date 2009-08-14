/**
   @file spawn.c

   This file implements process spawning and child exit signal catching. It also
   sends notifications on child exits.
   <p>
   Copyright (C) 2004-2009 Nokia Corporation.

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
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <sysexits.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <grp.h>
#include <pwd.h>
#include <stdbool.h>
#include <sched.h>


static void announce_child_exit(GPid pid, gint status, gpointer unused)
{
  DSM_MSGTYPE_PROCESS_EXITED msg =
    DSME_MSG_INIT(DSM_MSGTYPE_PROCESS_EXITED);

  msg.pid    = pid;
  msg.status = status;

  broadcast_internally(&msg);

  g_spawn_close_pid(pid);
}

/* TODO: use strspn() instead */
static int is_inside(const char *chars, const char *str)
{
  char *ptr;

  for (; *chars; chars++) {
      ptr = strchr(str, *chars);
      if (ptr)
          return 1;
  }

  return 0;
}

static bool make_argv(const char* cmdline, char*** argv, char** buf)
{
  *argv = NULL;
  *buf  = NULL;

  if (is_inside("\"'~*?[](){}$;<>", cmdline)) {
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

typedef struct child_setup {
  const char* cmdline;
  uid_t       uid;
  gid_t       gid;
  int         nice_val;
  int         oom_adj;
} child_setup;

static void setup_child(const child_setup* setup_data)
{
  const child_setup* p = setup_data;

  int i;
  int max_fd_count;

  max_fd_count = getdtablesize();
  if (max_fd_count == -1)  {
      max_fd_count = 256;
  }

  for (i = 3; i < max_fd_count; ++i) {
      close(i);
  }

  if (setsid() < 0) {
      fprintf(stderr, "'%s' failed to set session id", p->cmdline);
  }

  /* restore the default scheduler */
  struct sched_param sch;
  memset(&sch, 0, sizeof(sch));
  sch.sched_priority = 0;
  if (sched_setscheduler(0, SCHED_OTHER, &sch) == -1) {
      fprintf(stderr, "'%s' unable to set the scheduler", p->cmdline);
  }

  /* set the priority first to zero as dsme runs with -1,
   * then to the requested
   */
  if (setpriority(PRIO_PROCESS, 0, 0) != 0) {
      fprintf(stderr, "'%s' unable to set the priority to 0", p->cmdline);
  }
  if (p->nice_val != 0) {
      errno = 0;
      if (nice(p->nice_val) == -1 && errno != 0) {
          fprintf(stderr, "'%s' unable to nice to %i", p->cmdline, p->nice_val);
      }
  }
  if (p->oom_adj != 0) {
      if (!adjust_oom(p->oom_adj)) {
          fprintf(stderr,
                  "'%s' failed to set oom_adj; Unprotecting from OOM",
                  p->cmdline);
          if (!unprotect_from_oom()) {
              fprintf(stderr, "'%s' failed to unprotect from OOM", p->cmdline);
          }
      }
  } else if (p->nice_val >= 0) {
      if (!unprotect_from_oom()) {
          fprintf(stderr, "'%s' failed to unprotect from OOM", p->cmdline);
      }
  }

  struct passwd* passwd_field = getpwuid(p->uid);
  if (passwd_field != NULL) {
      if (initgroups(passwd_field->pw_name, passwd_field->pw_gid) == -1) {
          fprintf(stderr,
                  "'%s' initgroups() failed: %s",
                  p->cmdline,
                  strerror(errno));
          _exit(EX_NOPERM);
      }
  } else {
      fprintf(stderr,
              "'%s' requested UID (%d) not found, initgroups() not called",
              p->cmdline,
              p->uid);
  }

  if (setgid(p->gid)) {
      fprintf(stderr,
              "'%s' setgid(%d) => Permission denied",
              p->cmdline,
              p->gid);
      _exit(EX_NOPERM);
  }

  if (setuid(p->uid)) {
      fprintf(stderr,
              "'%s' setuid(%d) => Permission denied",
              p->cmdline,
              p->uid);
      _exit(EX_NOPERM);
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
  char*       cmdcopy = NULL;
  pid_t       pid;
  child_setup setup   = { cmdline, uid, gid, nice_val, oom_adj };

  if (make_argv(cmdline, &args, &cmdcopy)) {
      pid = fork();

      if (pid == 0) {

          /* child */
          setup_child(&setup);

          if (env) {
              execve(args[0], args, env);
          } else {
              execvp(args[0], args);
          }

          fprintf(stderr, "'%s' exec() failed: %s\n", cmdline, strerror(errno));
          exit(EXIT_FAILURE);

      } else if (pid == -1) {
          /* error */
          dsme_log(LOG_CRIT, "fork() failed: %s", strerror(errno));
      } else {
          /* parent */
          g_child_watch_add(pid, announce_child_exit, announce_child_exit);
          retval = pid;
      }

      free(cmdcopy);
      free(args);
  } else {
      dsme_log(LOG_CRIT, "error parsing cmdline: '%s'", cmdline);
  }

  return retval;
}


void spawn_shutdown(void) {
  // TODO: no guarantee that announce_child_exit is a unique value :(
  while (g_source_remove_by_user_data(announce_child_exit)) {
      // EMPTY LOOP
  }
}
