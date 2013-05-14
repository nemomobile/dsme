/**
   @file powerontimer_backend.c

   This file implements part of the device poweron timer feature and
   provides low level storage functionality to be used by dsme plugin.
   <p>
   Copyright (C) 2010 Nokia Corporation

   @author Simo Piiroinen <simo.piiroinen@nokia.com>

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

#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif

#include "powerontimer_backend.h"

#include <dsme/logging.h>

#include <sys/time.h>

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>

#include <cal.h>

/* ========================================================================= *
 * Configuration
 * ========================================================================= */

// prefix for log messages from this module
#define LOGPFIX "poweron-timer: "

/* ========================================================================= *
 * CAL block contents
 * ========================================================================= */

// cal block flags to be used
enum { pot_cal_flags = CAL_FLAG_USER };

// name of the cal block to be used
static const char pot_cal_name[] = "poweron-timer";

// cal block v0 -> just the version header
typedef struct
{
  int32_t version; /* Must be the first entry in every configuration.
                    * Initialize to zero.
                    * Actual value set by pot_import_cal_data().
                    */

} pot_cal_data_v0;

// cal block v1 -> data contained in v1
typedef struct
{
  int32_t version; // this will be set to 1

  int32_t poweron; // seconds of power on time ...
  int32_t uptime;  // ... recorded at point of uptime

  int32_t reboots; // number of reboots detected via uptime drops

  int32_t updates; // number of times the cal block has been written

} pot_cal_data_v1;

// cal block version currently expected by the logic
typedef pot_cal_data_v1 pot_cal_data;

/* ========================================================================= *
 * Generic Utilities
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * monotime_get  --  CLOCK_MONOTONIC -> struct timeval
 * ------------------------------------------------------------------------- */

static void monotime_get(struct timeval *tv)
{
  struct timespec ts = { 0, 0 };
  if( clock_gettime(CLOCK_MONOTONIC, &ts) == -1 )
  {
    dsme_log(LOG_WARNING, LOGPFIX"%s: %s", "CLOCK_MONOTONIC", strerror(errno));
  }
  TIMESPEC_TO_TIMEVAL(tv, &ts);
}

/* ------------------------------------------------------------------------- *
 * uptime_read  --  /proc/uptime -> struct timeval
 * ------------------------------------------------------------------------- */

static void uptime_read(struct timeval *tv)
{
  static const char path[]  = "/proc/uptime";

  double  secs = 0;
  FILE   *file = 0;

  char    data[256];

  if( (file = fopen(path, "r")) == 0 )
  {
    dsme_log(LOG_WARNING, LOGPFIX"%s: %s", path, strerror(errno));
    goto cleanup;
  }

  if( fgets(data, sizeof data, file ) == 0 )
  {
    dsme_log(LOG_WARNING, LOGPFIX"%s: %s", path, "unexpected EOF");
    goto cleanup;
  }

  if( (secs = strtod(data, 0)) <= 0.0 )
  {
    dsme_log(LOG_WARNING, LOGPFIX"%s: %s", path, "parsed non-positive uptime");
  }

cleanup:

  if( file != 0 ) fclose(file);

  tv->tv_sec  = (time_t)secs;
  tv->tv_usec = (suseconds_t)((secs - tv->tv_sec) * 1000000);

  if( tv->tv_usec < 0 )
  {
    tv->tv_usec += 1000000;
    tv->tv_sec  -= 1;
  }
}

/* ------------------------------------------------------------------------- *
 * uptime_get  --  /proc/uptime -> struct timeval via CLOCK_MONOTONIC
 * ------------------------------------------------------------------------- */

static time_t uptime_get(void)
{
  static bool           offset_ok = false;
  static struct timeval offset = { 0, 0 };

  struct timeval        monotime;

  // read uptime from /proc once then use monotonic clock
  // source to get up to date values without needing to
  // open, read, close & doing double precision calculation

  monotime_get(&monotime);

  if( !offset_ok )
  {
    offset_ok = true;
    uptime_read(&offset);
    timersub(&offset, &monotime, &offset);
  }

  timeradd(&monotime, &offset, &monotime);

  return monotime.tv_sec;
}

/* ========================================================================= *
 * CAL access
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * pot_import_cal_data  --  convert cal block data to current format
 * ------------------------------------------------------------------------- */

static int pot_import_cal_data(pot_cal_data *pot,
                               const void *data, size_t size)
{
  int err = -1;

  const pot_cal_data_v0 *v0 = data;
  const pot_cal_data_v1 *v1 = data;

  // reset existing data
  memset(pot, 0, sizeof pot);

  // import based on block version
  if( size < sizeof *v0 )
  {
    dsme_log(LOG_ERR, LOGPFIX"data block too small");
    goto cleanup;
  }

  switch( v0->version )
  {
  case 1:
    if( size < sizeof *v1 )
    {
      dsme_log(LOG_ERR, LOGPFIX"data block (v%"PRId32") %s",
               v0->version, "too small");
      goto cleanup;
    }

    pot->poweron = v1->poweron;
    pot->uptime  = v1->uptime;
    pot->reboots = v1->reboots;
    pot->updates = v1->updates;
    break;

  default:
    dsme_log(LOG_ERR, LOGPFIX"data block (v%"PRId32") %s",
             v0->version, "unkown");
    goto cleanup;
  }

  // no errors
  err = 0;

  /* dsme_log(LOG_INFO, LOGPFIX"data block (v%"PRId32") %s",
   *        v0->version, "imported");
   */

cleanup:

  // mark current version
  pot->version = 1;

  return err;
}

/* ------------------------------------------------------------------------- *
 * pot_read_cal  --  read block from cal & convert to current format
 * ------------------------------------------------------------------------- */

static int pot_read_cal(pot_cal_data *pot)
{
  int            err   = -1;
  struct cal    *cal   = 0;
  void          *data  = 0;
  unsigned long  size  = 0;

  if( cal_init(&cal) < 0 )
  {
    dsme_log(LOG_ERR, LOGPFIX"cal %s failed", "init");
    goto cleanup;
  }

  if( cal_read_block(cal, pot_cal_name, &data, &size, pot_cal_flags) < 0 )
  {
    // just a warning as it will be missing on the first boot
    dsme_log(LOG_WARNING, LOGPFIX"cal %s failed", "read");
    goto cleanup;
  }

  err = pot_import_cal_data(pot, data, size);

cleanup:

  if( cal != 0 ) cal_finish(cal);
  free(data);
  return err;
}

/* ------------------------------------------------------------------------- *
 * pot_write_cal  --  write block to cal
 * ------------------------------------------------------------------------- */

static int pot_write_cal(const pot_cal_data *pot)
{
  int         err = -1;
  struct cal *cal = 0;

  if( cal_init(&cal) < 0 )
  {
    dsme_log(LOG_ERR, LOGPFIX"cal %s failed", "init");
    goto cleanup;
  }

  if( cal_write_block(cal, pot_cal_name, pot, sizeof *pot, pot_cal_flags) < 0 )
  {
    dsme_log(LOG_ERR, LOGPFIX"cal %s failed", "write");
    goto cleanup;
  }

  err = 0;

cleanup:

  if( cal != 0 ) cal_finish(cal);

  return err;
}

/* ========================================================================= *
 * Cached CAL data
 * ========================================================================= */

// cal block contents - written back to periodically
static pot_cal_data cal =
{
  .version = 0,
  .poweron = 0,
  .uptime  = 0,
  .reboots = 0,
  .updates = 0,
};

// set when cal block contents have been read
static bool pot_cal_read_done = false;

// poweron time is increased only in USER mode
static bool pot_in_user_mode  = false;

/* ------------------------------------------------------------------------- *
 * pot_update_lim  --  calculate next cal write limit
 * ------------------------------------------------------------------------- */

static int32_t pot_update_lim(uint32_t poweron)
{
  // for making time thresholds more readable
  enum { S = 1, M = 60 * S, H = 60 * M, D = 24 * H, };

  // upto 8h of use -> write every 15 minutes
  if( poweron <  8*H ) return 15*M;

  // upto 7d of use -> write every hour
  if( poweron <  7*D ) return  1*H;

  // upto 30d of use -> write every 6 hours
  if( poweron < 30*D ) return  6*H;

  // after write save once per day
  return 1*D;
}

/* ------------------------------------------------------------------------- *
 * pot_update_cal  --  update power on timer data, write to cal when needed
 * ------------------------------------------------------------------------- */

void pot_update_cal(bool user_mode, bool force_save)
{
  static bool pending_save  = false;

  int32_t uptime_now  = 0;
  int32_t poweron_dif = 0;
  int32_t poweron_lim = 0;

  if( !pot_cal_read_done )
  {
    pot_cal_read_done = true;
    pot_read_cal(&cal);
  }

  uptime_now  = uptime_get();
  poweron_dif = uptime_now - cal.uptime;

  if( poweron_dif < 0 )
  {
    // reboot has occured
    cal.reboots += 1;
    poweron_dif  = uptime_now;
    pending_save = true;
  }

  if( !pot_in_user_mode )
  {
    // counter is not incremented while we are not in user mode
    poweron_dif = 0;

    if( user_mode )
    {
      // transition to user mode -> save on first chance
      pending_save = true;
    }
  }

  // update frequency depends on power on time stored at cal
  poweron_lim = pot_update_lim(cal.poweron);

  // When to save ... the logic is ugly, but boils down to:
  // 1. when forced
  // 2. in user mode & update is large enough
  // 3. (reboot or state transition) and update is larger than zero

  if( force_save || pending_save || (poweron_dif >= poweron_lim) )
  {
    cal.uptime   = uptime_now;

    if( force_save || (poweron_dif > 0) )
    {
      cal.updates  += 1;
      cal.poweron += poweron_dif;
      pot_write_cal(&cal);
      pending_save = false;
    }
    else
    {
      pending_save = true;
    }
  }

  pot_in_user_mode = user_mode;
}

/* ------------------------------------------------------------------------- *
 * pot_get_poweron_secs  --  query current poweron timer value
 * ------------------------------------------------------------------------- */

int32_t pot_get_poweron_secs(void)
{
  // we should never have a situation where query over IPC
  // happens before the cal data has been fetched, but we
  // still need to prepare for that occasion ...
  //
  if( !pot_cal_read_done )
  {
    pot_cal_read_done = true;
    pot_read_cal(&cal);
  }

  // mimic total time calculation as done in
  // the pot_update_cal() function

  int32_t uptime_now  = 0;
  int32_t poweron_dif = 0;

  uptime_now = uptime_get();

  if( (poweron_dif = uptime_now - cal.uptime) < 0 )
  {
    // cal not yet updated after reboot
    poweron_dif = uptime_now;
  }

  if( !pot_in_user_mode )
  {
    // timer advances only in user mode
    poweron_dif = 0;
  }

  return cal.poweron + poweron_dif;
}
