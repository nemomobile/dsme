/**
   @file logging.h

   Prototypes for logging functions.
   <p>
   Copyright (C) 2004-2010 Nokia Corporation.

   @author Tuukka Tikkanen

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

#ifndef DSME_LOGGING_H
#define DSME_LOGGING_H

#include <dsme/messages.h>
/* Even if syslog is not used, use the message levels therein */
#include <syslog.h>
#include <stdbool.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Logging methods */
typedef enum {
    LOG_METHOD_NONE,   /* Suppress all the messages */
    LOG_METHOD_STI,    /* Serial trace interface */
    LOG_METHOD_STDOUT, /* Print messages to stdout */
    LOG_METHOD_STDERR, /* Print messages to stderr */
    LOG_METHOD_SYSLOG, /* Use syslog(3) */
    LOG_METHOD_FILE    /* Output messages to the file */
} log_method;


/**
   Logs a single message.

   @param level Message level, one of the following:
      @arg @c LOG_CRIT - critical failure, DSME must exit
      @arg @c LOG_ERR - error condition, possibly resulting in
                        reduced functionality
      @arg @c LOG_WARNING - abnormal condition, possibly resulting in
                        reduced functionality
      @arg @c LOG_NOTICE - normal, but significant, condition
      @arg @c LOG_INFO - informational message
      @arg @c LOG_DEBUG - debugging message
   @param fmt Printf-style format string.
   @param ... Message variables.
*/

/* Function prototypes */
#ifdef DSME_LOG_ENABLE
/* Function prototypes */
void dsme_log_txt(int level, const char *fmt, ...)
    __attribute__((format(printf,2,3)));
void dsme_log_raw(int level, const char *fmt, ...) __attribute__((format(printf,2,3)));

/* Macros */
#define dsme_log(level, fmt...) dsme_log_txt(level, fmt)
#else
#define dsme_log(level, fmt...)
#endif


typedef struct {
    DSMEMSG_PRIVATE_FIELDS
    int verbosity;
} DSM_MSGTYPE_SET_LOGGING_VERBOSITY;

enum {
    DSME_MSG_ENUM(DSM_MSGTYPE_SET_LOGGING_VERBOSITY, 0x00001103),
};


/**
   Initializes the logging subsystem.
*/
bool dsme_log_open(log_method method, int verbosity, int usetime,
                   const char *prefix, int facility, int option,
                   const char *filename);

void dsme_log_set_verbosity(int verbosity);


/**
   Flushes and shuts down the logging subsystem.
*/
void dsme_log_close(void);

/**
   Ask logging thread to stop
*/
void dsme_log_stop(void);

char* pid2text(pid_t pid);

#ifdef __cplusplus
}
#endif

#endif
