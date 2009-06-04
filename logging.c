/**
   @file logging.c

   Implements DSME logging functionality.
   <p>
   Copyright (C) 2004-2009 Nokia Corporation.

   @author Yuri Zaporogets

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

/* Logging is only enabled if this is defined */
#ifdef DSME_LOG_ENABLE

/* STI channel number for dsme traces */
#define DSME_STI_CHANNEL 44

/* Function prototypes */
static void log_to_null(int prio, const char* fmt, va_list ap);
static void log_to_stderr(int prio, const char* fmt, va_list ap);

/* This variable holds the address of the logging functions */
static void (*dsme_log_routine)(int prio, const char* fmt, va_list ap) =
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
static void log_to_null(int prio, const char* fmt, va_list ap)
{
}


/*
 * This routine is used when STI logging method is set
 */
static void log_to_sti(int prio, const char* fmt, va_list ap)
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
            vsnprintf(buf+len, sizeof(buf)-len, fmt, ap);
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
        vfprintf(stderr, fmt, ap);
    }
}


/*
 * This routine is used when stdout logging method is set
 */
static void log_to_stdout(int prio, const char* fmt, va_list ap)
{
    if (logopt.verbosity >= prio) {
        if (prio >= 0)
            printf("%s %s: ", logopt.prefix, log_prio_str(prio));
        vfprintf(stdout, fmt, ap);
        fprintf(stdout, "\n");
    }
}


/*
 * This routine is used when stderr logging method is set
 */
static void log_to_stderr(int prio, const char* fmt, va_list ap)
{
    if (logopt.verbosity >= prio) {
        if (prio >= 0)
            fprintf(stderr, "%s %s: ", logopt.prefix, log_prio_str(prio));
        vfprintf(stderr, fmt, ap);
        fprintf(stderr, "\n");
    }
}


/*
 * This routine is used when syslog logging method is set
 */
static void log_to_syslog(int prio, const char* fmt, va_list ap)
{
    if (logopt.verbosity >= prio) {
        if (prio < 0) prio = LOG_DEBUG;
        vsyslog(prio, fmt, ap);
    }
}


/*
 * This routine is used when file logging method is set
 */
static void log_to_file(int prio, const char* fmt, va_list ap)
{
    if (logopt.verbosity >= prio) {
        if (prio >= 0)
            fprintf(logopt.filep, "%s %s: ", logopt.prefix, log_prio_str(prio));
        vfprintf(logopt.filep, fmt, ap);
        fprintf(logopt.filep, "\n");
        fflush(logopt.filep);
    }
}


/*
 * This function is for "system" log messages.
 */
int dsme_log_txt(int level, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    dsme_log_routine(level, fmt, ap);
    /* also output to console for critical errors and more important */
    if (level <= LOG_CRIT) {
        vfprintf(stderr, fmt, ap);
        fprintf(stderr, "\n");
    }
    va_end(ap);
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
int dsme_log_open(log_method  method,
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
                return -1;
            }
            dsme_log_routine = log_to_file;
            break;

        default:
            return -1;
    }
    return 0;
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
