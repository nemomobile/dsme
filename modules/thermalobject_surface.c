/**
   @file thermalobject_surface.c

   This file implements a thermal object for tracking
   device surface temperatures.
   <p>
   Copyright (C) 2009-2010 Nokia Corporation

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
#include "dsme-wdd-wd.h"

#include "dsme/modules.h"
#include "dsme/logging.h"


static bool get_surface_temperature(thermal_object_t*         thermal_object,
                                    temperature_handler_fn_t* callback);

static thermal_object_configuration_t surface_thermal_conf = {
  "surface",
  {
      /* (min, max], interval */
      {    -1,  52,        60 / DSME_HEARTBEAT_INTERVAL }, /* NORMAL  */
      {    52,  58,        60 / DSME_HEARTBEAT_INTERVAL }, /* WARNING */
      {    58,  98,        30 / DSME_HEARTBEAT_INTERVAL }, /* ALERT   */
      {    98,  99,        30 / DSME_HEARTBEAT_INTERVAL }, /* FATAL   */
  },
  get_surface_temperature
};

static thermal_object_t surface_thermal_object = {
  &surface_thermal_conf,
  THERMAL_STATUS_NORMAL,
  false
};

static temperature_handler_fn_t* handle_temperature;

static void report_surface_temperature(void* object, int temperature)
{
  /* Thermal algorithm for estimating surface temperature: */
  /*
   * Based on measurement findings brought forth in Thermal measurement
   * results/manager review session on 2009-05-18, the surface temperature
   * can be estimated by subtracting 7 deg C from battery temperature:
   */
  temperature = temperature - 7;

  if (handle_temperature) {
      thermal_object_t* thermal_object = object;
      handle_temperature(thermal_object, temperature);
  }
}

static bool get_surface_temperature(thermal_object_t*         thermal_object,
                                    temperature_handler_fn_t* callback)
{
  handle_temperature = callback;

  return dsme_request_battery_temperature(thermal_object,
                                          report_surface_temperature);
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
