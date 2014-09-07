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
#ifdef DSME_SYSTEMD_ENABLE
#include <systemd/sd-daemon.h>
#endif
#include <sys/resource.h>

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

static volatile bool dsme_abnormal_exit = false;

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
#ifdef DSME_SYSTEMD_ENABLE
        case SIGUSR1:
            /* Inform systemd that server is initialized */
            sd_notify(0, "READY=1");
            break;
#endif
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
    const char*  short_options = "dhsp:l:v:";
    const struct option long_options[] = {
        { "help",           0, NULL, 'h' },
        { "verbosity",      0, NULL, 'v' },
#ifdef DSME_SYSTEMD_ENABLE
        { "systemd",        0, NULL, 's' },
#endif
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
        fprintf(stderr, ME "fcntl failed: %s\n", strerror(errno));
    } else if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        fprintf(stderr, ME "fcntl(O_NONBLOCK) failed: %s\n", strerror(errno));
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
            dsme_abnormal_exit = true;
            goto done_running;
        }

        // ping the dsme server
        ping(pipe_to_child);
        ++child_ping_count;
    }

done_running:
    return;
}

/** Flag for alarm timeout occured during waitpid() in kill_and_wait() */
static volatile bool wait_and_kill_tmo = false;

/** Alarm callback for terminating waitpid() in kill_and_wait()
 *
 * @param sig SIGALRM (not used)
 */
static void wait_and_kill_tmo_cb(int sig)
{
  wait_and_kill_tmo = true;
}

/** Try to kill a child process and wait for it to exit
 *
 * @param pid       process to kill
 * @param sig       signal to send
 * @param max_wait  seconds to wait for child exit
 *
 * return true if child exit was caught, false otherwise
 */
static bool kill_and_wait(pid_t pid, int sig, int max_wait)
{
  bool res = false;

  /* Use alarm() to provide EINTR timeout for waitpid() via
   * SIGALRM that explicitly does not use SA_RESTART flag */
  struct sigaction sa =
  {
    .sa_flags = SA_RESETHAND,
    .sa_handler = wait_and_kill_tmo_cb,
  };
  alarm(0);
  sigaction(SIGALRM, &sa, 0);
  wait_and_kill_tmo = false;
  alarm(max_wait);

  /* Send the signal to child process */
  if( kill(pid, sig) == -1 ) {
    fprintf(stderr, ME "failed to kill child process %d: %m\n", (int)pid);
    goto EXIT;
  }

  /* Wait for child exit */
  while( !wait_and_kill_tmo )
  {
    int status = 0;
    int options = 0;
    int rc = waitpid(pid, &status, options);

    if( rc < 0 ) {
      if( errno != EINTR ) {
        fprintf(stderr, ME "process %d did not exit: %m; giving up\n", (int)pid);
        goto EXIT;
      }

      if( wait_and_kill_tmo ) {
        fprintf(stderr, ME "child process %d did not exit; timeout\n", (int)pid);
        goto EXIT;
      }

      fprintf(stderr, ME "process %d did not exit: %m; retrying\n", (int)pid);
    }
    else if( rc == pid ) {
      if( WIFEXITED(status) ) {
        fprintf(stderr, ME "child exit value: %d\n", WEXITSTATUS(status));
      }
      if( WIFSIGNALED(status) ) {
        fprintf(stderr, ME "child exit signal: %s\n",
                 strsignal(WTERMSIG(status)));
      }
      res = true;
      break;
    }
  }

EXIT:

  /* Cancel alarm and reset signal handler back to defaults */
  alarm(0);
  signal(SIGALRM, SIG_DFL);

  return res;
}

/** Wakelock that DSME "leaks" on abnormal exit
 *
 * The purpose of the leak is to block late suspend while
 * dsme is not running, so that:
 * 1) suspend does not inhibit systemd from restarting dsme
 *    or rebooting the device
 * 2) repeating suspend/resume cycles do not feed the hw
 *    watchdog and we get the watchdog reboot if dsme restart
 *    does not succeed
 */
#define DSME_RESTART_WAKELOCK "dsme_restart"

/** Sysfs helper for wakelock manipulation
 */
static void wakelock_write(const char *path, const char *data, size_t size)
{
    // NOTE: called from signal handler - must stay async-signal-safe

    int fd = open(path, O_WRONLY);
    if( fd != -1 ) {
        if( write(fd, data, size) == -1 ) {
            /* dontcare, but need to keep the compiler happy */
        }
        close(fd);
    }
}

/** Get restart wakelock
 *
 * Used for blocking suspend for one minute when dsme restart
 * or watchdog reboot is expected to happen.
 */
static void obtain_restart_wakelock(void)
{
    // NOTE: called from signal handler - must stay async-signal-safe

    static const char path[] = "/sys/power/wake_lock";
    static const char text[] = DSME_RESTART_WAKELOCK " 60000000000\n";
    wakelock_write(path, text, sizeof text - 1);
}

/** Clear restart wakelock
 *
 * Used when dsme makes successful startup or normal exit
 */
static void release_restart_wakelock(void)
{
    static const char path[] = "/sys/power/wake_unlock";
    static const char text[] = DSME_RESTART_WAKELOCK "\n";
    wakelock_write(path, text, sizeof text - 1);
}

/** Set wakelock before invoking default signal handler
 *
 * If dsme dies due to signal, set wakelock with timeout before
 * invoking default signal handler.
 *
 * The wakelock will be cleared on dsme restart.
 *
 * The timeout must be long enough to allow watchdog reboot to
 * happen if dsme restart fails.
 */
static void handle_terminating_signal(int sig)
{
    // NOTE: signal handler - must stay async-signal-safe

    /* Do not try anything fancy if we have been here
     * before or are already on abnormal exit path */
    if( !dsme_abnormal_exit ) {
        dsme_abnormal_exit = true;

        /* restore default signal handler */
        signal(sig, SIG_DFL);

        /* get a wakelock and kick the watchdogs */
        obtain_restart_wakelock();
        dsme_wd_kick_from_sighnd();

        /* invoke default signal handler */
        raise(sig);
    }
    _exit(EXIT_FAILURE);
}

/** Trap signals that normally terminate a process
 *
 * Some of these will be overridden later on.
 */
static void trap_terminating_signals(void)
{
    static const int lut[] =
    {
        SIGHUP, SIGINT, SIGQUIT, SIGILL, SIGABRT, SIGFPE, SIGSEGV,
        SIGPIPE, SIGALRM, SIGTERM, SIGUSR1, SIGUSR2, SIGBUS
    };

    for( size_t i = 0; i < sizeof lut / sizeof *lut; ++i )
        signal(lut[i], handle_terminating_signal);
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

    trap_terminating_signals();

    // set up signal handler
    signal(SIGHUP,  signal_handler);
    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, signal_handler);
    signal(SIGCHLD, signal_handler);
#ifdef DSME_SYSTEMD_ENABLE
    signal(SIGUSR1, signal_handler);
#endif


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
            _exit(EXIT_FAILURE);
        }
        if (dup2(from_child[1], STDOUT_FILENO) != STDOUT_FILENO) {
            fprintf(stderr, ME "dup2 failed: %s\n", strerror(errno));
            _exit(EXIT_FAILURE);
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
        _exit(EXIT_FAILURE);

    } else {
        // parent

        // close child ends of pipes & set parent ends of pipes non-blocking
        close(to_child[0]);
        close(from_child[1]);
        set_nonblocking(to_child[1]);
        set_nonblocking(from_child[0]);
    }

    /* Before entering the mainloop, clear wakelock that might be set if dsme
     * is restarting after signal / watchdog pingpong failure */
    release_restart_wakelock();

    unsigned sleep_interval = DSME_HEARTBEAT_INTERVAL;
    mainloop(sleep_interval, to_child[1], from_child[0]);
    fprintf(stderr, ME "Exited main loop, quitting\n");

    /* Get wakelock after exiting the mainloop, will be cleared
     * if we make orderly normal exit */
    obtain_restart_wakelock();

    /* Bring down the dsme-server child process
     *
     * Note: The maximum duration we can spend here must be shorter
     *       than both hw watchdog kick period and the time systemd
     *       allows for dsme itself to exit.
     */

    // kick watchdogs so we have time to wait for dsme-server to exit
    dsme_wd_kick();

    if( kill_and_wait(pid, SIGTERM, 8) || kill_and_wait(pid, SIGKILL, 3) ) {
        // clear nowayout states and close watchdog files
        dsme_wd_quit();
    }
    else {
        // leave the nowayout states on so that we will get a wd reboot
        // shortly after dsme exits, but kick the watchdogs one more time
        // to give the system some time to make orderly shutdown
        dsme_wd_kick();
        fprintf(stderr, ME "dsme-server stop failed, leaving watchdogs"
                " active\n");
        dsme_abnormal_exit = true;
    }

    /* Remove the PID file */
    if (remove(DSME_PID_FILE) < 0 && errno != ENOENT) {
        fprintf(stderr, ME "Couldn't remove lockfile: %m\n");
    }

    /* Clear wakelock on normal, successful exit */
    if( dsme_abnormal_exit )
        fprintf(stderr, ME "abnormal exit, leaving wakelock active\n");
    else
        release_restart_wakelock();

    fprintf(stderr, "DSME %s terminating\n", STRINGIFY(PRG_VERSION));
    return dsme_abnormal_exit ? EXIT_FAILURE : EXIT_SUCCESS;
}
