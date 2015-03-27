/**
   @file timers.c

   Implementation of DSME timers.
   <p>
   Copyright (C) 2004-2010 Nokia Corporation.

   @author Ari Saastamoinen
   @authot Semi Malinen <semi.malinen@nokia.com>

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

#include "../include/dsme/timers.h"
#include <glib.h>


dsme_timer_t dsme_create_timer(unsigned               seconds,
                               dsme_timer_callback_t* callback,
                               void*                  data)
{
  return g_timeout_add_seconds(seconds, callback, data);
}


dsme_timer_t dsme_create_timer_high_priority(unsigned               seconds,
                                             dsme_timer_callback_t* callback,
                                             void*                  data)
{
  return g_timeout_add_full(G_PRIORITY_HIGH, 1000*seconds, callback, data, 0);
}


void dsme_destroy_timer(dsme_timer_t timer)
{
  g_source_remove(timer); // TODO: or g_source_destroy()?
}
