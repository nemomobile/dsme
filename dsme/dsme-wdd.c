/**
   @file dsme_wdd.c

   This file implements the main() of the DSME HW watchdog daemon.
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

#ifndef __cplusplus
#define _GNU_SOURCE
#endif

#include "dsme-wdd.h"
#include "dsme-wdd-wd.h"
#include "dsme/oom.h"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <time.h>
#include <fcntl.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/wait.h>

#define STRINGIFY(x)  STRINGIFY2(x)
#define STRINGIFY2(x) #x

#define DSME_SERVER_PATH DSME_SBIN_PATH"/dsme-server"
#define DSME_PID_FILE    "/tmp/dsme.pid" // TODO: is this needed?

#define DSME_NICE         (-20)      /* least niceness */
#define DSME_HB_SCHEDULER SCHED_FIFO /* real-time scheduling */

#define DSME_MAXPING 5


static void signal_handler(int  signum);
static void usage(const char *  progname);
static int  daemonize(void);
static void mainloop(unsigned sleep_interval,
                     int      pipe_to_child,
                     int      pipe_from_child);


static volatile bool run = true;


/**
   Usage
*/
static void usage(const char *  progname)
{
    printf("USAGE: %s [-d] options\n",
           progname);
    printf("Valid options:\n");
    printf(" -d  --daemon      "
             "Detach from terminal and run in background\n");
    printf(" -h  --help        Help\n");
    printf("All other options are passed to %s\n", DSME_SERVER_PATH);
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
            fprintf(stderr, ME "daemonize: fork failed: %s\n", strerror(errno));
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
    if (i == -1) {
        i = 256;
    }
    while (--i >= 0) {
        close(i);
    }

    i = open("/dev/null", O_RDWR);
    i = open("/dev/console", O_RDWR);
    if (dup(i) == -1) {
        fprintf(stderr, ME "daemonize: dup failed: %s\n", strerror(errno));
        return -1;
    }

    /* set umask */
    /* umask() */

    /* single instance */
    i = open(DSME_PID_FILE, O_RDWR | O_CREAT, 0640);
    if (i < 0) {
        fprintf(stderr, ME "Can't open lockfile. Exiting.\n");
        exit(EXIT_FAILURE);
    }
    if (lockf(i, F_TLOCK, 0) < 0) {
        fprintf(stderr, ME "Already running. Exiting.\n");
        exit(EXIT_FAILURE);
    }

    sprintf(str, "%d\n", getpid());
    if (write(i, str, strlen(str)) == -1) {
        fprintf(stderr, ME "daemonize: write failed: %s\n", strerror(errno));
        return -1;
    }
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
        case SIGCHLD:
        case SIGINT:
        case SIGTERM:
            run = false;
            break;
    }
}

#ifdef DSME_LOG_ENABLE
static int        logging_verbosity = LOG_INFO;
static log_method logging_method    = LOG_METHOD_SYSLOG;
#endif

static void parse_options(int   argc,   /* in  */
                          char* argv[], /* in  */
                          int*  daemon) /* out */
{
    int          next_option;
    const char*  program_name  = argv[0];
    const char*  short_options = "dhp:l:v:";
    const struct option long_options[] = {
        { "help",           0, NULL, 'h' },
        { "verbosity",      0, NULL, 'v' },
#ifdef DSME_LOG_ENABLE  
        { "logging",        0, NULL, 'l' },
#endif
        { "daemon",         0, NULL, 'd' },
        { 0, 0, 0, 0 }
    };

    if (daemon) { *daemon = 0; }

    while ((next_option =
            getopt_long(argc, argv, short_options, long_options,0)) != -1)
    {
        switch (next_option) {

            case 'd': /* -d or --daemon */
                if (daemon) *daemon = 1;
                break;
            case 'h': /* -h or --help */
                usage(program_name);
                break;

            case '?': /* Unreckgnized option */
                usage(program_name);
                break;
        }
    }
}


static bool set_nonblocking(int fd)
{
    bool set = false;
    int  flags;

    errno = 0;
    if ((flags = fcntl(fd, F_GETFL)) == -1 && errno != 0) {
        fprintf(stderr, ME "fcntl failed: %s", strerror(errno));
    } else if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        fprintf(stderr, ME "fcntl(O_NONBLOCK) failed: %s", strerror(errno));
    } else {
        set = true;
    }

    return set;
}


static void ping(int pipe)
{
    ssize_t bytes_written;
    while ((bytes_written = write(pipe, "*", 1)) == -1 &&
           (errno == EINTR))
    {
        // EMPTY LOOP
    }
}

static bool pong(int pipe)
{
    bool got_pong = false;

    while (true) {
        ssize_t bytes_read;
        char    dummy[DSME_MAXPING];
        while ((bytes_read = read(pipe, dummy, sizeof(dummy))) ==
               -1 &&
               errno == EINTR)
        {
            // EMPTY LOOP
        }
        if (bytes_read >= 1) {
            // got something
            got_pong = true;
            if (bytes_read == sizeof(dummy)) {
                // keep reading to empty the pipe
                continue;
            } else {
                // already emptied the pipe
                break;
            }
        } else {
            // either there is nothing to read from the pipe,
            // the pipe has been closed, or
            // there was an error
            break;
        }
    }

    return got_pong;
}


static void mainloop(unsigned sleep_interval,
                     int      pipe_to_child,
                     int      pipe_from_child)
{
    int child_ping_count = 0;

    // NOTE: One should NOT do anything potentially blocking in the mainloop.
    //       Otherwise we run the risk of being late to kick the HW watchdogs.
    while (run) {
        // kick WD's right before sleep
        dsme_wd_kick();

        // sleep precisely
        struct timespec remaining_sleep_time = { sleep_interval, 0 };
        while (nanosleep(&remaining_sleep_time, &remaining_sleep_time) == -1 &&
               errno == EINTR)
        {
            // interrupt could mean that someone wants to force a WD kick;
            // also, we have to kick just before stopping running
            dsme_wd_kick();
            if (!run) {
                goto done_running;
            }

            // interrupt may have come from the dsme server; consider it alive
            child_ping_count = 0;
        }

        // kick WD's right after sleep
        dsme_wd_kick();

        // make sure the dsme server (the child) is alive
        if (pong(pipe_from_child)) {
            child_ping_count = 0;
        } else if (child_ping_count >= DSME_MAXPING) {
            // dsme server has failed to respond in due time
            fprintf(stderr, ME "dsme-server nonresponsive; quitting\n");
            run = false;
            goto done_running;
        }

        // ping the dsme server
        ping(pipe_to_child);
        ++child_ping_count;
    }

done_running:
    return;
}


/**
  @todo Possibility to alter priority of initial module somehow
  */
int main(int argc, char *argv[])
{
    fprintf(stderr, "DSME %s starting up\n", STRINGIFY(PRG_VERSION));

    // do the first kick right away
    if (!dsme_wd_init()) {
        fprintf(stderr, ME "no WD's opened; WD kicking disabled\n");
    }
    dsme_wd_kick();

    // set up signal handler
    signal(SIGHUP,  signal_handler);
    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, signal_handler);
    signal(SIGCHLD, signal_handler);


    // protect from oom
    if (!protect_from_oom()) {
        fprintf(stderr, ME "Couldn't protect from oom: %s\n", strerror(errno));
    }

    // set priority/niceness (will be inherited)
    if (setpriority(PRIO_PROCESS, 0, DSME_NICE) == -1) {
        fprintf(stderr, ME "Couldn't set the priority: %s\n", strerror(errno));
    }

    // set scheduler (will be inherited)
    struct sched_param sch;
    memset(&sch, 0, sizeof(sch));
    sch.sched_priority = sched_get_priority_max(DSME_HB_SCHEDULER);
    if (sched_setscheduler(0, DSME_HB_SCHEDULER, &sch) == -1) {
        fprintf(stderr, ME "Couldn't set the scheduler: %s\n", strerror(errno));
    }

    // lock to ram (will not be inherited)
    if (mlockall(MCL_CURRENT|MCL_FUTURE) == -1) {
        fprintf(stderr, ME "Couldn't lock to RAM: %s\n", strerror(errno));
    }


    // parse command line options
    int daemon = 0;
    parse_options(argc, argv, &daemon);

    // daemonize
    if (daemon && daemonize() == -1) {
        return EXIT_FAILURE;
    }

    // set running directory
    if (chdir("/") == -1) {
        fprintf(stderr, ME "chdir failed: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    // open communication pipes
    int to_child[2];
    int from_child[2];
    if (pipe(to_child) != 0 || pipe(from_child) != 0) {
        fprintf(stderr, ME "pipe failed: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    // fork and exec the dsme server
    pid_t pid;
    if ((pid = fork()) == -1) {
        fprintf(stderr, ME "fork failed: %s\n", strerror(errno));
        return EXIT_FAILURE;
    } else if (pid == 0) {
        // child

        // TODO: lower priority
        // TODO: restore scheduler

        // child gets the pipes in stdin & stdout
        if (dup2(to_child[0], STDIN_FILENO) != STDIN_FILENO) {
            fprintf(stderr, ME "dup2 failed: %s\n", strerror(errno));
            return EXIT_FAILURE;
        }
        if (dup2(from_child[1], STDOUT_FILENO) != STDOUT_FILENO) {
            fprintf(stderr, ME "dup2 failed: %s\n", strerror(errno));
            return EXIT_FAILURE;
        }

        // close all the other descriptors
        int max_fd_count = getdtablesize();
        if (max_fd_count == -1) {
            max_fd_count = 256;
        }
        for (int i = 3; i < max_fd_count; ++i) {
            (void)close(i);
        }

        // exec dsme server core
        char* newargv[argc+1];
        newargv[0] = (char*)DSME_SERVER_PATH;
        for (int i = 1; i < argc; ++i) {
            newargv[i] = argv[i];
        }
        newargv[argc] = 0;
        execv(DSME_SERVER_PATH, newargv);
        fprintf(stderr,
                ME "execv failed: %s: %s\n",
                DSME_SERVER_PATH,
                strerror(errno));
        return EXIT_FAILURE;

    } else {
        // parent

        // close child ends of pipes & set parent ends of pipes non-blocking
        close(to_child[0]);
        close(from_child[1]);
        set_nonblocking(to_child[1]);
        set_nonblocking(from_child[0]);
    }

    unsigned sleep_interval = DSME_HEARTBEAT_INTERVAL;
#if 0
    fprintf(stderr,
            ME "Entering main loop, with %u s interval\n",
            sleep_interval);
#endif
    mainloop(sleep_interval, to_child[1], from_child[0]);
    fprintf(stderr, ME "Exited main loop, quitting\n");

    // also bring dsme server down
    kill(pid, SIGTERM);
    (void)waitpid(pid, 0, 0);

    if (remove(DSME_PID_FILE) < 0) {
        fprintf(stderr, ME "Couldn't remove lockfile\n");
    }

    return EXIT_SUCCESS;
}
