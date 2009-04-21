/**
   @file timers.h

   Defines structures and function prototypes for using DSME timers.
   <p>
   Copyright (C) 2004-2009 Nokia Corporation.

   @author Ari Saastamoinen
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

#ifndef DSME_TIMERS_H
#define DSME_TIMERS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef int dsme_timer_callback_t(void* data);

typedef unsigned dsme_timer_t;

/**
   Creates a new DSME timer.

   @param seconds  Timer expiry in seconds from current time
   @param callback Function to be called when the timer expires
   @param data	   Passed to callback function as an argument.
   @return !0 on success; 0 on failure
           pointer is returned.
*/
dsme_timer_t dsme_create_timer(unsigned               seconds,
                               dsme_timer_callback_t* callback,
                               void*                  data);

dsme_timer_t dsme_create_timer_high_priority(unsigned               seconds,
                                             dsme_timer_callback_t* callback,
                                             void*                  data);


/**
   Deactivates and destroys an existing timer.

   @param timer	Timer to be destroyed.
*/
void dsme_destroy_timer(dsme_timer_t timer);


#ifdef __cplusplus
}
#endif

#endif
