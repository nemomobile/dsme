/**
   @file dsme.c

   This file implements the main function and main loop of DSME component.
   <p>
   Copyright (C) 2004-2009 Nokia Corporation.

   @author Ari Saastamoinen
   @author Ismo Laitinen <ismo.laitinen@nokia.com>
   @author Yuri Zaporogets
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

/*
 * TODO: - things to glibify:
 *         -- sockets to use g_io_channels
 *         -- plug-ins
 */

#ifndef __cplusplus
#define _GNU_SOURCE
#endif

#include "dsme/mainloop.h"
#include "dsme/modulebase.h"
#include "dsme/dsmesock.h"
#include "dsme/protocol.h"
#include "dsme/logging.h"
#include "dsme/dsme-cal.h"
#include "dsme/messages.h"
#include "dsme/oom.h"

#include <glib.h>
#include <unistd.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <dlfcn.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/resource.h>

static void signal_handler(int  signum);
static void usage(const char *  progname);
static int  daemonize(void);

#define DSME_PRIORITY (-1)
#define PID_FILE      "/tmp/dsme.pid"

#define ArraySize(a) ((int)(sizeof(a)/sizeof(*a)))

/**
   Usage
*/
static void usage(const char *  progname)
{
  printf("USAGE: %s -p <startup-module> "
           "[-p <optional-module>] [...] options\n",
         progname);
  printf("Valid options:\n");
  printf(" -d  --daemon      "
           "Detach from terminal and run in background\n");
#ifdef DSME_LOG_ENABLE
  printf(" -l  --logging     "
           "Logging type (syslog, sti, stderr, stdout, none)\n");
  printf(" -v  --verbosity   Log verbosity (3..7)\n");
#endif
  printf(" -h  --help        Help\n");
}

/**
   Daemonizes the program

   @return On success, zero is returned. On error, -1 is returned.
*/
static int daemonize(void)
{
  int  i = 0;
  char str[10];

  /* Detach from process group */
  switch (fork()) {
    case -1:
      /* An error occurred */
      dsme_log(LOG_CRIT, "daemonize: fork failed: %s", strerror(errno));
      return -1;

    default:
      /* Parent, terminate */
      exit(EXIT_SUCCESS);
      /* Not reached */
      return -1;

    case 0:
      /* Child (daemon) continues */
      break;
  }


  /* Detach tty */
  setsid();

  /* Close all file descriptors and redirect stdio to /dev/null */
  i = getdtablesize();
  if (i == -1)
      i = 256;

  while (--i >= 0)
      close(i);

  i = open("/dev/null", O_RDWR);
  i = open("/dev/console", O_RDWR);
  dup(i);

  /* set umask */
  /* umask() */

  /* single instance */
  i = open(PID_FILE, O_RDWR | O_CREAT, 0640);
  if (i < 0) {
      dsme_log(LOG_CRIT, "Can't open lockfile. Exiting.");
      exit(EXIT_FAILURE);
  }
  if (lockf(i, F_TLOCK, 0) < 0) {
      dsme_log(LOG_CRIT, "Already running. Exiting.");
      exit(EXIT_FAILURE);
  }

  sprintf(str, "%d\n", getpid());
  write(i, str, strlen(str));
  close(i);


  /* signals */
  signal(SIGTSTP, SIG_IGN);   /* ignore tty signals */
  signal(SIGTTOU, SIG_IGN);
  signal(SIGTTIN, SIG_IGN);


  return 0;
}

/**
 * Signal_Handler
 *
 * @param sig signal_type
 */
void signal_handler(int sig)
{
  switch (sig) {
    case SIGPIPE:
#if 0 /* must not syslog within a signal handler */
      dsme_log(LOG_ERR, "SIGPIPE received, some client exited before noticed?");
#endif
      break;
    case SIGHUP:
      /*      DLOG_NOTICE, "Restarting..."); */
      break;
    case SIGINT:
    case SIGTERM:
      dsme_main_loop_quit();
      break;
  }
}

#ifdef DSME_LOG_ENABLE  
static int        logging_verbosity = LOG_INFO;
static log_method logging_method    = LOG_METHOD_SYSLOG;
#endif

static void parse_options(int      argc,           /* in  */
                          char*    argv[],         /* in  */
                          GSList** module_names,   /* out */
                          int*     daemon)         /* out */
{
  int          next_option;
  const char*  program_name  = argv[0];
  const char*  short_options = "dhp:l:v:";
  const struct option long_options[] = {
        { "startup-module", 1, NULL, 'p' },
        { "help",           0, NULL, 'h' },
        { "verbosity",      0, NULL, 'v' },
#ifdef DSME_LOG_ENABLE  
        { "logging",        0, NULL, 'l' },
#endif
        { "daemon",         0, NULL, 'd' },
        { 0, 0, 0, 0 }
  };

  if (daemon)         { *daemon         = 0; }

  while ((next_option =
          getopt_long(argc, argv, short_options, long_options,0)) != -1)
    {
      switch (next_option) {
        case 'p': /* -p or --startup-module, allow only once */
        {
          if (module_names) {
            *module_names = g_slist_append(*module_names, optarg);
          }
        }
          break;

        case 'd': /* -d or --daemon */
          if (daemon) *daemon = 1;
          break;
#ifdef DSME_LOG_ENABLE  
        case 'l': /* -l or --logging */
        {
          const char *log_method_name[] = {
              "none",   /* LOG_METHOD_NONE */
              "sti",    /* LOG_METHOD_STI */
              "stdout", /* LOG_METHOD_STDOUT */
              "stderr", /* LOG_METHOD_STDERR */
              "syslog", /* LOG_METHOD_SYSLOG */
              "file"    /* LOG_METHOD_FILE */
          };
          int i;

          for (i = 0; i < ArraySize(log_method_name); i++) {
              if (!strcmp(optarg, log_method_name[i])) {
                  logging_method = (log_method)i;
                  break;
              }
          }
          if (i == ArraySize(log_method_name))
              fprintf(stderr, "Ignoring invalid logging method %s\n", optarg);
        }
          break;
        case 'v': /* -v or --verbosity */
          if (strlen(optarg) == 1 && isdigit(optarg[0]))
              logging_verbosity = atoi(optarg);
          break;
#else
        case 'l':
        case 'v':
          printf("Logging not compiled in\n");
          break;
#endif  
        case 'h': /* -h or --help */
          usage(program_name);
          exit(EXIT_SUCCESS);

        case '?': /* Unrecognized option */
          usage(program_name);
          exit(EXIT_FAILURE);
      }
    }

  /* check if unknown parameters were given */
  if (optind < argc) {
      usage(program_name);
      exit(EXIT_FAILURE);
  }
}

static bool receive_and_queue_message(dsmesock_connection_t* conn)
{
  bool keep_connection = true;

  dsmemsg_generic_t* msg;
  msg = (dsmemsg_generic_t*)dsmesock_receive(conn);
  if (msg) {
      broadcast_internally_from_socket(msg, conn);
      if (DSMEMSG_CAST(DSM_MSGTYPE_CLOSE, msg)) {
        keep_connection = false;
      }
      free(msg);
  }

  return keep_connection;
}

/**
  @todo Possibility to alter priority of initial module somehow
  */
int main(int argc, char *argv[])
{
  GSList* module_names = 0;
  int     daemon       = 0;

  signal(SIGHUP,  signal_handler);
  signal(SIGINT,  signal_handler);
  signal(SIGTERM, signal_handler);
  signal(SIGPIPE, signal_handler);

  dsme_cal_init();

  parse_options(argc, argv, &module_names, &daemon);

  if (!module_names) {
      usage(argv[0]);
      return EXIT_FAILURE;
  }

  if (daemon && daemonize() == -1) {
      return EXIT_FAILURE;
  }

#ifdef DSME_LOG_ENABLE  
  dsme_log_open(logging_method,
                logging_verbosity,
                0,
                "DSME",
                0,
                0,
                "/var/log/dsme.log");
#endif

  if (setpriority(PRIO_PROCESS, 0, DSME_PRIORITY) != 0) {
      dsme_log(LOG_ERR, "Couldn't set the priority");
  }

  /* protect DSME from oom; notice that this must be done before any
   * calls to pthread_create() in order to have all threads protected
   */
  if (protect_from_oom() != 0) {
      dsme_log(LOG_ERR, "Couldn't protect from oom");
  }

  /* load modules */
  if (!modulebase_init(module_names)) {
      g_slist_free(module_names);
#ifdef DSME_LOG_ENABLE  
      dsme_log_close();
#endif
      return EXIT_FAILURE;
  }
  g_slist_free(module_names);

  /* init socket communication */
  if (dsmesock_listen(receive_and_queue_message) == -1) {
      dsme_log(LOG_CRIT, "Error creating DSM socket: %s", strerror(errno));
#ifdef DSME_LOG_ENABLE  
      dsme_log_close();
#endif
      return EXIT_FAILURE;
  }

  /* set running directory */
  chdir("/");

  dsme_log(LOG_DEBUG, "Entering main loop");
  dsme_main_loop_run(process_message_queue);
  dsme_log(LOG_DEBUG, "Exiting main loop");

  if (remove(PID_FILE) < 0) {
      dsme_log(LOG_ERR, "Couldn't remove lockfile");
  }

  dsmesock_shutdown();

  modulebase_shutdown();

#ifdef DSME_LOG_ENABLE
  dsme_log_close();
#endif

  return EXIT_SUCCESS;
}
