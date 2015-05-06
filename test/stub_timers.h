/**
   @file stub_timers.h

   A simple test driver and test cases for DSME
   <p>
   Copyright (C) 2011 Nokia Corporation

   @author Jyrki Hämäläinen <ext-jyrki.hamalainen@nokia.com>

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


#ifndef DSME_TEST_STUB_TIMERS_H
#define DSME_TEST_STUB_TIMERS_H

#include "../include/dsme/timers.h"

typedef struct test_timer_t {
    unsigned               seconds;
    dsme_timer_callback_t* callback;
    void*                  data;
} test_timer_t;

static unsigned      timers = 0;
static test_timer_t* timer  = 0;

static void reset_timers(void)
{
  free(timer);
  timer  = 0;
  timers = 0;
}

static inline bool timer_exists(void)
{
  int count = 0;
  int i;

  for (i = 0; i < timers; ++i) {
      if (timer[i].seconds != 0) {
          ++count;
      }
  }

  if (count == 1) {
      fprintf(stderr, "[=> 1 timer exists]\n");
  } else if (count) {
      fprintf(stderr, "[=> %d timers exist]\n", count);
  } else {
      fprintf(stderr, "[=> no timers]\n");
  }

  return count;
}

static inline unsigned first_timer_seconds(void)
{
  unsigned timeout = 0;

  /* first find the earliest timer... */
  int earliest = -1;
  int           i;

  for (i = 0; i < timers; ++i) {
      if (timer[i].seconds != 0) {
          if (earliest < 0 || timer[i].seconds < timer[earliest].seconds) {
              earliest = i;
          }
      }
  }

  /* ...then call it */
  assert(earliest >= 0);
  timeout = timer[earliest].seconds;
  return timeout;
}

static inline void trigger_timer(void)
{
  /* first find the earliest timer... */
  int earliest = -1;
  int           i;

  for (i = 0; i < timers; ++i) {
      if (timer[i].seconds != 0) {
          if (earliest < 0 || timer[i].seconds < timer[earliest].seconds) {
              earliest = i;
          }
      }
  }

  /* ...then call it */
  assert(earliest >= 0);
  fprintf(stderr, "\n[TRIGGER TIMER %d]\n", earliest + 1);
  if (!timer[earliest].callback(timer[earliest].data)) {
      timer[earliest].seconds = 0;
  }
}

static int dsme_create_timer_fails = 0;

dsme_timer_t dsme_create_timer(unsigned               seconds,
                               dsme_timer_callback_t* callback,
                               void*                  data)
{
  if (!dsme_create_timer_fails) {
      ++timers;
      timer = realloc(timer, timers * sizeof(*timer));
      timer[timers - 1].seconds  = seconds;
      timer[timers - 1].callback = callback;
      timer[timers - 1].data     = data;
      fprintf(stderr, "[=> timer %u created for %u s]\n", timers, seconds);
      return timers;
  } else {
      --dsme_create_timer_fails;
      return 0;
  }
}

void dsme_destroy_timer(dsme_timer_t t)
{
  fprintf(stderr, "[=> destroying timer %u]\n", t);
  assert(t > 0 && t <= timers);
  assert(timer[t - 1].seconds != 0);
  timer[t - 1].seconds  = 0;
  timer[t - 1].callback = 0;
  timer[t - 1].data     = 0;
}

#endif /* DSME_TEST_STUB_TIMERS_H */
