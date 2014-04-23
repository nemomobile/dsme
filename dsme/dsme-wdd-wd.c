/**
   @file dsme-wd-wdd.c

   This file implements hardware watchdog kicker.
   <p>
   Copyright (C) 2004-2010 Nokia Corporation.

   @author Igor Stoppa <igor.stopaa@nokia.com>
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

#define _GNU_SOURCE

#include "dsme-wdd-wd.h"
#include "dsme-wdd.h"

#include "dsme-rd-mode.h"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <linux/watchdog.h>


#define DSME_STATIC_STRLEN(s) (sizeof(s) - 1)


typedef struct wd_t {
    const char* file;   /* pathname of the watchdog device */
    int         period; /* watchdog timeout (s); 0 for keeping the default */
    const char* flag;   /* R&D flag in cal that disables the watchdog */
} wd_t;

/* the table of HW watchdogs; notice that their order matters! */
#define SHORTEST DSME_SHORTEST_WD_PERIOD
static const wd_t wd[] = {
    /* path,               timeout (s), disabling R&D flag */
    {  "/dev/watchdog",    SHORTEST,    "no-omap-wd" }, /* omap wd      */
    {  "/dev/watchdog0",   SHORTEST,    "no-omap-wd" }, /* omap wd      */
    {  "/dev/watchdog1",   SHORTEST,    "no-omap-wd" }, /* omap wd      */
    {  "/dev/twl4030_wdt", 30,          "no-ext-wd"  }, /* twl (ext) wd */
};

#define WD_COUNT (sizeof(wd) / sizeof(wd[0]))

/* watchdog file descriptors */
static int  wd_fd[WD_COUNT];


void dsme_wd_kick(void)
{
  int i;
  int dummy;

  for (i = 0; i < WD_COUNT; ++i) {
      if (wd_fd[i] != -1) {
          int bytes_written;
          while ((bytes_written = write(wd_fd[i], "*", 1)) == -1 &&
                 errno == EAGAIN)
          {
              const char msg[] = "Got EAGAIN when kicking WD ";
              dummy = write(STDERR_FILENO, msg, DSME_STATIC_STRLEN(msg));
              dummy = write(STDERR_FILENO, wd[i].file, strlen(wd[i].file));
              dummy = write(STDERR_FILENO, "\n", 1);
          }
          if (bytes_written != 1) {
              const char msg[] = "Error kicking WD ";

              dummy = write(STDERR_FILENO, msg, DSME_STATIC_STRLEN(msg));
              dummy = write(STDERR_FILENO, wd[i].file, strlen(wd[i].file));
              dummy = write(STDERR_FILENO, "\n", 1);
              if (dummy < 0) {
                  break;
              }

              /* must not kick later wd's if an earlier one fails */
              break;
          }
      }
  }
}

void dsme_wd_kick_from_sighnd(void)
{
    // NOTE: called from signal handler - must stay async-signal-safe

    for( size_t i = 0; i < WD_COUNT; ++i) {
        if( wd_fd[i] == -1 )
            continue;
        if( write(wd_fd[i], "*", 1) == -1 ) {
            /* dontcare, but need to keep the compiler happy */
        }
    }
}

static void check_for_wd_flags(bool wd_enabled[])
{
    unsigned long len  = 0;
    const char*   p = NULL;
    int           i;

    p = dsme_rd_mode_get_flags();
    if (p) {
        len = strlen(p);
        if (len > 1) {
            for (i = 0; i < WD_COUNT; ++i) {
                if (strstr(p, wd[i].flag)) {
                    wd_enabled[i] = false;
                    fprintf(stderr, ME "WD kicking disabled: %s\n", wd[i].file);
                }
            }
        } else {
            fprintf(stderr, ME "No WD flags found, WD kicking enabled\n");
        }
    }

    return;
}

bool dsme_wd_init(void)
{
    int  opened_wd_count = 0;
    bool wd_enabled[WD_COUNT];
    int  i;

    for (i = 0; i < WD_COUNT; ++i) {
        wd_enabled[i] = true; /* enable all watchdogs by default */
        wd_fd[i]      = -1;
    }

    /* disable the watchdogs that have a disabling R&D flag */
    check_for_wd_flags(wd_enabled);

    /* open enabled watchdog devices */
    for (i = 0; i < WD_COUNT; ++i) {
        if (wd_enabled[i] == false)
            continue;

        /* try to open watchdog device node */
	if( (wd_fd[i] = open(wd[i].file, O_RDWR)) == -1 ) {
	    if( errno != ENOENT )
		fprintf(stderr,
			ME "Error opening WD %s: %s\n",
			wd[i].file,
			strerror(errno));
	    continue;
        }

        ++opened_wd_count;

        if (wd[i].period != 0) {
            /* set the wd period */
            /* ioctl() will overwrite tmp with the time left */
            int tmp = wd[i].period;
            if (ioctl(wd_fd[i], WDIOC_SETTIMEOUT, &tmp) != 0) {
                fprintf(stderr,
                         ME "Error setting WD period for %s\n",
                         wd[i].file);
            }
        } else {
            fprintf(stderr,
                     ME "Keeping default WD period for %s\n",
                     wd[i].file);
        }
    }

    if( opened_wd_count < 1 )
	fprintf(stderr, ME "Could not open any watchdog files");

    return (opened_wd_count != 0);
}

void dsme_wd_quit(void)
{
    for( size_t i = 0; i < WD_COUNT; ++i ) {
	int fd = wd_fd[i];

	if( fd == -1 )
	    continue;

	/* Remove the fd from the array already before attempting to
	 * close it so that dsme_wd_kick_from_sighnd() does not have
	 * a chance to use stale file descriptors */
	wd_fd[i] = -1;

	if( TEMP_FAILURE_RETRY(write(fd, "V", 1)) == -1 ) {
	    fprintf(stderr, ME "%s: failed to clear nowayout: %m\n",
		    wd[i].file);
	}
	else {
	    fprintf(stderr, ME "%s: cleared nowayout state\n",
		    wd[i].file);
	}

	if( TEMP_FAILURE_RETRY(close(fd)) == -1 ) {
	    fprintf(stderr, ME "%s: failed to close file: %m\n",
		    wd[i].file);
	}
    }
}
