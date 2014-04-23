/**
   @file thermalmanager.h

   DSME thermal object abstraction
   <p>
   Copyright (C) 2009-2010 Nokia Corporation.

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
#ifndef THERMALMANAGER_H
#define THERMALMANAGER_H

#include "dsme/timers.h"
#include <stdbool.h>

#define INVALID_TEMPERATURE -9999

typedef enum {
  THERMAL_STATUS_LOW,
  THERMAL_STATUS_NORMAL,
  THERMAL_STATUS_WARNING,
  THERMAL_STATUS_ALERT,
  THERMAL_STATUS_FATAL,

  THERMAL_STATUS_COUNT
} THERMAL_STATUS;

typedef struct thermal_status_configuration_t {
  int min;     /* min and max set the status range: [min, max) */
  int max;
  int mintime; /* temperature polling minimum interval in seconds */
  int maxtime; /* temperature polling maximum interval in seconds */
} thermal_status_configuration_t;

typedef struct thermal_object_t thermal_object_t;

typedef void (temperature_handler_fn_t)(thermal_object_t* thermal_object,
                                        int               temperature);

typedef struct thermal_object_configuration_t {
  const char*                    name;
  thermal_status_configuration_t state[THERMAL_STATUS_COUNT];
  bool                         (*request_temperature)(
                                     thermal_object_t*         thermal_object,
                                     temperature_handler_fn_t* callback);
} thermal_object_configuration_t;

struct thermal_object_t {
  thermal_object_configuration_t* conf;
  THERMAL_STATUS                  status;
  THERMAL_STATUS                  new_status;
  int                             status_change_count;
  bool                            request_pending;
};


extern void dsme_register_thermal_object(thermal_object_t* thermal_object);

extern void dsme_unregister_thermal_object(thermal_object_t* thermal_object);

#endif
