/**
   @file dsme.c

   This file implements the main function and main loop of DSME component.
   <p>
   Copyright (C) 2004-2010 Nokia Corporation.

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
 *         -- plug-ins
 */

#ifndef __cplusplus
#define _GNU_SOURCE
#endif

#include "../include/dsme/mainloop.h"
#include "../include/dsme/modulebase.h"
#include "../include/dsme/dsmesock.h"
#include <dsme/protocol.h>
#include "../include/dsme/logging.h"
#include <dsme/messages.h>
#include "../include/dsme/oom.h"

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
#include <sched.h>

static void signal_handler(int  signum);
static void usage(const char *  progname);

#define DSME_PRIORITY (-1)

#define ArraySize(a) ((int)(sizeof(a)/sizeof(*a)))

#define ME "DSME: "

/**
   Usage
*/
static void usage(const char *  progname)
{
  fprintf(stderr, "USAGE: %s -p <startup-module> "
                    "[-p <optional-module>] [...] options\n",
         progname);
  fprintf(stderr, "Valid options:\n");
#ifdef DSME_LOG_ENABLE
  fprintf(stderr, " -l  --logging     "
                    "Logging type (syslog, sti, stderr, stdout, none)\n");
  fprintf(stderr, " -v  --verbosity   Log verbosity (3..7)\n");
#endif
#ifdef DSME_SYSTEMD_ENABLE
  fprintf(stderr, " -s  --systemd     "
                    "Signal systemd when initialization is done\n");
#endif
  fprintf(stderr, " -h  --help        Help\n");
}


/**
 * Signal_Handler
 *
 * @param sig signal_type
 */
void signal_handler(int sig)
{
  switch (sig) {
    case SIGINT:
    case SIGTERM:
      dsme_main_loop_quit(EXIT_SUCCESS);
      break;
  }
}

#ifdef DSME_LOG_ENABLE
static int        logging_verbosity = LOG_NOTICE;
static log_method logging_method    = LOG_METHOD_SYSLOG;
#endif
#ifdef DSME_SYSTEMD_ENABLE
static int signal_systemd = 0;
#endif

static void parse_options(int      argc,           /* in  */
                          char*    argv[],         /* in  */
                          GSList** module_names)   /* out */
{
  int          next_option;
  const char*  program_name  = argv[0];
  const char*  short_options = "dhsp:l:v:";
  const struct option long_options[] = {
        { "startup-module", 1, NULL, 'p' },
        { "help",           0, NULL, 'h' },
        { "verbosity",      0, NULL, 'v' },
#ifdef DSME_SYSTEMD_ENABLE
        { "systemd",        0, NULL, 's' },
#endif
#ifdef DSME_LOG_ENABLE  
        { "logging",        0, NULL, 'l' },
#endif
        { 0, 0, 0, 0 }
  };

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
              fprintf(stderr,
                      ME "Ignoring invalid logging method %s\n",
                      optarg);
        }
          break;
        case 'v': /* -v or --verbosity */
          if (strlen(optarg) == 1 && isdigit(optarg[0]))
              logging_verbosity = atoi(optarg);
          break;
#else
        case 'l':
        case 'v':
          fprintf(stderr, ME "Logging not compiled in\n");
          break;
#endif  
#ifdef DSME_SYSTEMD_ENABLE
        case 's': /* -s or --systemd */
          signal_systemd = 1;
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
  bool                               keep_connection = true;
  DSM_MSGTYPE_SET_LOGGING_VERBOSITY* logging;

  dsmemsg_generic_t* msg;
  msg = (dsmemsg_generic_t*)dsmesock_receive(conn);
  if (msg) {
      broadcast_internally_from_socket(msg, conn);
      if (DSMEMSG_CAST(DSM_MSGTYPE_CLOSE, msg)) {
        keep_connection = false;
      } else if ((logging = DSMEMSG_CAST(DSM_MSGTYPE_SET_LOGGING_VERBOSITY,
                                         msg)))
      {
          dsme_log_set_verbosity(logging->verbosity);
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

  signal(SIGINT,  signal_handler);
  signal(SIGTERM, signal_handler);
  signal(SIGHUP,  signal_handler);
  signal(SIGPIPE, signal_handler);

  /* protect DSME from oom; notice that this must be done before any
   * calls to pthread_create() in order to have all threads protected
   */
  if (!protect_from_oom()) {
      fprintf(stderr, ME "Couldn't protect from oom: %s\n", strerror(errno));
  }

  /* Set static priority for RT-scheduling */
  int scheduler;
  struct sched_param param;
  scheduler = sched_getscheduler(0);
  if(sched_getparam(0, &param) == 0) {
      param.sched_priority = sched_get_priority_min(scheduler);
      if(sched_setparam(0, &param) != 0) {
          fprintf(stderr, ME "Couldn't set static priority: %s\n", strerror(errno));
      }
  }
  else {
      fprintf(stderr, ME "Couldn't get scheduling params: %s\n", strerror(errno));
  }

  /* Set nice value for cases when dsme-server is not under RT-scheduling*/
  if (setpriority(PRIO_PROCESS, 0, DSME_PRIORITY) != 0) {
      fprintf(stderr, ME "Couldn't set dynamic priority: %s\n", strerror(errno));
  }

  parse_options(argc, argv, &module_names);

  if (!module_names) {
      usage(argv[0]);
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
  if (chdir("/") == -1) {
      dsme_log(LOG_CRIT, "chdir failed: %s", strerror(errno));
      return EXIT_FAILURE;
  }
#ifdef DSME_SYSTEMD_ENABLE
  /* Inform main process that we are ready 
   * Main process will inform systemd
   */
  if (signal_systemd) {
      kill(getppid(), SIGUSR1);
  }
#endif
  dsme_log(LOG_DEBUG, "Entering main loop");
  dsme_main_loop_run(process_message_queue);
  dsme_log(LOG_CRIT, "Exited main loop, quitting");

  dsmesock_shutdown();

  modulebase_shutdown();

#ifdef DSME_LOG_ENABLE
  dsme_log_close();
#endif

  return dsme_main_loop_exit_code();
}
