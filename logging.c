/**
   @file logging.c

   Implements DSME logging functionality.
   <p>
   Copyright (C) 2004-2009 Nokia Corporation.

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

#define _GNU_SOURCE // TODO: should these be put to makefile?

#include "dsme/logging.h"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <sys/syslog.h>
#include <sys/socket.h>
#include <asm/types.h>
#include <linux/netlink.h>

#include <pthread.h>
#include <semaphore.h>

/* Logging is only enabled if this is defined */
#ifdef DSME_LOG_ENABLE

/* STI channel number for dsme traces */
#define DSME_STI_CHANNEL 44

/* Function prototypes */
static void log_to_null(int prio, const char* message);
static void log_to_stderr(int prio, const char* message);

/* This variable holds the address of the logging functions */
static void (*dsme_log_routine)(int prio, const char* message) =
  log_to_stderr;

static struct {
    log_method  method;    /* Chosen logging method */
    int         verbosity; /* Verbosity level (corresponding to LOG_*) */
    int         usetime;   /* Timestamps on/off */
    const char* prefix;    /* Message prefix */
    FILE*       filep;     /* Log file stream */
    int         sock;      /* Netlink socket for STI method */
    int         channel;   /* Channel number for STI method */
} logopt = { LOG_METHOD_STDERR, LOG_INFO, 0, "DSME", NULL };


#define DSME_MAX_LOG_MESSAGE_LENGTH 123
#define DSME_MAX_LOG_BUFFER_ENTRIES  64 /* must be a power of 2! */

typedef struct log_entry {
    int  prio;
    char message[DSME_MAX_LOG_MESSAGE_LENGTH + 1];
} log_entry;

/* ring buffer for log entries */
static log_entry ring_buffer[DSME_MAX_LOG_BUFFER_ENTRIES];

/* ring buffer access counters */

/* ring buffer semaphore */
static sem_t ring_buffer_sem;

/* ring buffer write and read counters */
static volatile unsigned write_count = 0;
static volatile unsigned read_count  = 0;

// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
// SIMO HACKING
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

void dsme_log_raw(int level, const char *fmt, ...) {
  if (logopt.verbosity >= level) {
    va_list va; char *msg = 0;
    va_start(va, fmt);
    vasprintf(&msg, fmt, va);
    va_end(va);
    if( msg ) dsme_log_routine(level, msg), free(msg);
  }
}

void dsme_log_wakeup(void) {
  sem_post(&ring_buffer_sem);
}

#define DSME_MAX_LOG_CALLBACKS 4

static void (*log_cb[DSME_MAX_LOG_CALLBACKS])(void);

int dsme_log_cb_attach(void (*fn)(void))
{
  int i;
  for( i = 0; i < DSME_MAX_LOG_CALLBACKS; ++i ) {
    if( log_cb[i] == 0 ) {
      log_cb[i] = fn;
      return 1;
    }
  }
  return 0;
}

int dsme_log_cb_detach(void (*fn)(void))
{
  int i;
  for( i = 0; i < DSME_MAX_LOG_CALLBACKS; ++i ) {
    if( log_cb[i] == fn ) {
      log_cb[i] = 0;
      return 1;
    }
  }
  return 0;
}

static void dsme_log_cb_execute_all(void)
{
  int i;
  for( i = 0; i < DSME_MAX_LOG_CALLBACKS; ++i ) {
    if( log_cb[i] != 0 ) log_cb[i]();
  }
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
// SIMO HACKING
// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

/*
 * This routine returns the string corresponding to the logging priority
 */
static const char* log_prio_str(int prio)
{
    switch(prio) {
        case LOG_DEBUG:
            return "debug";
        case LOG_INFO:
            return "info";
        case LOG_NOTICE:
            return "notice";
        case LOG_WARNING:
            return "warning";
        case LOG_ERR:
            return "error";
        case LOG_CRIT:
            return "critical";
        case LOG_ALERT:
            return "alert";
        case LOG_EMERG:
            return "emergency";
        default:
            return "log";
    }
}

/*
 * Empty routine for suppressing all log messages
 */
static void log_to_null(int prio, const char* message)
{
}


/*
 * This routine is used when STI logging method is set
 */
static void log_to_sti(int prio, const char* message)
{
    if (logopt.sock != -1) {
        if (logopt.verbosity >= prio) {
            char buf[256];
            int len;
            struct nlmsghdr nlh;
            struct sockaddr_nl snl;

            if (prio >= 0) {
                snprintf(buf,
                         sizeof(buf),
                         "%s %s: ",
                         logopt.prefix,
                         log_prio_str(prio));
            }
            len = strlen(buf);
            snprintf(buf+len, sizeof(buf)-len, "%s", message);
            len = strlen(buf);

            struct iovec iov[2];
            iov[0].iov_base = &nlh;
            iov[0].iov_len  = sizeof(struct nlmsghdr);
            iov[1].iov_base = buf;
            iov[1].iov_len  = len;

            struct msghdr msg;
            msg.msg_name    = (void *)&snl;
            msg.msg_namelen = sizeof(struct sockaddr_nl);
            msg.msg_iov     = iov;
            msg.msg_iovlen  = sizeof(iov)/sizeof(*iov);

            memset(&snl, 0, sizeof(struct sockaddr_nl));

            snl.nl_family   = AF_NETLINK;
            nlh.nlmsg_len   = NLMSG_LENGTH(len);
            nlh.nlmsg_type  = (0xC0 << 8) | (1 << 0); /* STI Write */
            nlh.nlmsg_flags = (logopt.channel << 8);

            sendmsg(logopt.sock, &msg, 0);
        }
    } else {
        fprintf(stderr, "dsme trace: ");
        fprintf(stderr, "%s", message);
    }
}


/*
 * This routine is used when stdout logging method is set
 */
static void log_to_stdout(int prio, const char* message)
{
    if (logopt.verbosity >= prio) {
        if (prio >= 0) {
            fprintf(stdout, "%s %s: ", logopt.prefix, log_prio_str(prio));
        }
        fprintf(stdout, "%s\n", message);
    }
}


/*
 * This routine is used when stderr logging method is set
 */
static void log_to_stderr(int prio, const char* message)
{
    if (logopt.verbosity >= prio) {
        if (prio >= 0) {
            fprintf(stderr, "%s %s: ", logopt.prefix, log_prio_str(prio));
        }
        fprintf(stderr, "%s\n", message);
    }
}


/*
 * This routine is used when syslog logging method is set
 */
static void log_to_syslog(int prio, const char* message)
{
    if (logopt.verbosity >= prio) {
        if (prio < 0) prio = LOG_DEBUG;
        syslog(prio, "%s", message);
    }
}


/*
 * This routine is used when file logging method is set
 */
static void log_to_file(int prio, const char* message)
{
    if (logopt.verbosity >= prio) {
        if (prio >= 0)
            fprintf(logopt.filep, "%s %s: ", logopt.prefix, log_prio_str(prio));
        fprintf(logopt.filep, "%s\n", message);
        fflush(logopt.filep);
    }
}


/*
 * This function is the main entry point for logging.
 * It writes log entries to a ring buffer that the logging thread reads.
 */
void dsme_log_txt(int level, const char* fmt, ...)
{
    va_list         ap;

    va_start(ap, fmt);

    if (logopt.verbosity >= level) {
        /* buffer for the logging thread to log */
        log_entry* entry =
            &ring_buffer[write_count % DSME_MAX_LOG_BUFFER_ENTRIES];

        entry->prio = level;
        vsnprintf(entry->message,
                  DSME_MAX_LOG_MESSAGE_LENGTH + 1,
                  fmt,
                  ap);
        entry->message[DSME_MAX_LOG_MESSAGE_LENGTH] = '\0';

        ++write_count;

        /* wake up the logging thread */
        sem_post(&ring_buffer_sem);
    }

    /* always output critical messages to console */
    if (level <= LOG_CRIT) {
        vfprintf(stderr, fmt, ap);
        fprintf(stderr, "\n");
    }

    va_end(ap);
}

/*
 * This is the logging thread that reads log entries from
 * the ring buffer and writes them to their final destination.
 */
static void* logging_thread(void* param)
{

    while (true) {
        while (sem_wait(&ring_buffer_sem) == -1) {
            continue;
        }

        if( read_count != write_count ) {
	    log_entry* entry =
		 &ring_buffer[read_count % DSME_MAX_LOG_BUFFER_ENTRIES];
	    
	    dsme_log_routine(entry->prio, entry->message);
	    
	    ++read_count;
	}

        dsme_log_cb_execute_all();
    }

    return 0;
}

/*
 * Logging initialization. Parameters are:
 *   method   - logging method
 *   usetime  - if nonzero, each message will pe prepended with a timestamp
 *   prefix   - the text that will be printed before each message
 *   facility - the facility for openlog() (only for syslog method)
 *   option   - option for openlog() (only for syslog method)
 *   filename - log file name (only for file method)
 *
 * Returns 0 upon successfull initialization, a negative value otherwise.
 */
bool dsme_log_open(log_method  method,
                   int         verbosity,
                   int         usetime,
                   const char* prefix,
                   int         facility,
                   int         option,
                   const char* filename)
{
    logopt.method = method;
    logopt.verbosity = (verbosity > LOG_DEBUG) ? LOG_DEBUG :
        (verbosity < LOG_ERR) ? LOG_ERR : verbosity;
    logopt.usetime = usetime;
    logopt.prefix = prefix;

    switch (method) {

        case LOG_METHOD_NONE:
            dsme_log_routine = log_to_null;
            break;

        case LOG_METHOD_STI:
            {
                struct sockaddr_nl snl;
                int ret;

                memset(&snl, 0, sizeof(struct sockaddr_nl));
                snl.nl_family = AF_NETLINK;
                snl.nl_pid    = getpid();

                logopt.sock = ret = socket(PF_NETLINK,
                                           SOCK_RAW,
                                           NETLINK_USERSOCK);
                if (ret < 0)
                    goto out;

                ret = bind(logopt.sock, (struct sockaddr *)&snl,
                           sizeof(struct sockaddr_nl));
                if (ret < 0)
                    goto out;

                logopt.channel = DSME_STI_CHANNEL;
                dsme_log_routine = log_to_sti;
                break;
out:
                fprintf(stderr,
                        "STI init failed, will fall back to stderr method\n");
                dsme_log_routine = log_to_stderr;    
            }
        case LOG_METHOD_STDOUT:
            dsme_log_routine = log_to_stdout;
            break;

        case LOG_METHOD_STDERR:
            dsme_log_routine = log_to_stderr;
            break;

        case LOG_METHOD_SYSLOG:
            openlog(prefix, option, facility);
            dsme_log_routine = log_to_syslog;
            break;

        case LOG_METHOD_FILE:
            if ((logopt.filep = fopen(filename, "a")) == NULL) {
                fprintf(stderr,
                        "Can't create log file %s (%s)\n",
                        filename,
                        strerror(errno));
                return false;
            }
            dsme_log_routine = log_to_file;
            break;

        default:
            return false;
    }

    /* initialize the ring buffer semaphore */
    if (sem_init(&ring_buffer_sem, 0, 0) == -1) {
        fprintf(stderr, "sem_init: %s\n", strerror(errno));
        return false;
    }

    /* create the logging thread */
    pthread_attr_t     tattr;
    pthread_t          tid;
    struct sched_param param;

    if (pthread_attr_init(&tattr) != 0) {
        fprintf(stderr, "Error getting thread attributes\n");
        return false;
    }
    if (pthread_attr_getschedparam(&tattr, &param) != 0) {
        fprintf(stderr, "Error gettint scheduling parameters\n");
        return false;
    }
    if (pthread_create(&tid, &tattr, logging_thread, 0) != 0) {
        fprintf(stderr, "Error creating the logging thread\n");
        return false;
    }

    return true;
}


/*
 * This routine should be called before program termination. It will close
 * log streams and do other cleanup work.
 */
void dsme_log_close(void)
{
    switch (logopt.method) {
        case LOG_METHOD_STDOUT:
            fflush(stdout);
            break;
        case LOG_METHOD_STDERR:
            fflush(stderr);
            break;
        case LOG_METHOD_SYSLOG:
            closelog();
            break;
        case LOG_METHOD_FILE:
            fclose(logopt.filep);
            break;
        case LOG_METHOD_STI:
            close(logopt.sock);
            break;
        default:
            return;
    }
}

#endif /* DSME_LOG_ENABLE */
