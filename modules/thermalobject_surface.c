/**
   @file thermalobject_surface.c

   This file implements a thermal object for tracking
   device surface temperatures.
   <p>
   Copyright (C) 2009 Nokia Corporation

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

#include "thermalsensor_battery.h"
#include "thermalmanager.h"

#include "dsme/modules.h"
#include "dsme/logging.h"


static bool get_surface_temperature(int* temperature);

static thermal_object_configuration_t surface_thermal_conf = {
  "surface",
  {
      /* (min, max], interval */
      {    -1,  52,        60 }, /* NORMAL  */
      {    52,  58,        60 }, /* WARNING */
      {    58,  65,        30 }, /* ALERT   */
      {    65,  99,        30 }, /* FATAL   */
  },
  get_surface_temperature
};

static thermal_object_t surface_thermal_object = {
  &surface_thermal_conf,
  THERMAL_STATUS_NORMAL,
  0
};


static bool get_surface_temperature(int* temperature)
{
  /* This is where the thermal algorithm for surface temperature would go. */
  /* However, at the moment, there is none; we use battery temp directly. */
  return dsme_get_battery_temperature(temperature);
}


void module_init(module_t* handle)
{
  dsme_log(LOG_DEBUG, "libthermalobject_surface.so loaded");

  dsme_register_thermal_object(&surface_thermal_object);
}

void module_fini(void)
{
  dsme_unregister_thermal_object(&surface_thermal_object);

  dsme_log(LOG_DEBUG, "libthermalobject_surface.so unloaded");
}
