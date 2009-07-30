/**
   @file thermalobject_memory.c

   This file implements a thermal object for tracking memory temperatures.
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

#include "thermalsensor_omap.h"
#include "thermalmanager.h"

#include "dsme/modules.h"
#include "dsme/logging.h"


static bool get_memory_temperature(int* temperature);

static thermal_object_configuration_t memory_thermal_conf = {
  "memory",
  {
      /* (min, max], interval */
      {    -1,  55,        30 / DSME_HEARTBEAT_INTERVAL }, /* NORMAL  */
      {    55,  60,         5 / DSME_HEARTBEAT_INTERVAL }, /* WARNING */
      {    60,  82,         1 / DSME_HEARTBEAT_INTERVAL }, /* ALERT   */
      {    82,  99,         1 / DSME_HEARTBEAT_INTERVAL }  /* FATAL   */
  },
  get_memory_temperature
};

static thermal_object_t memory_thermal_object = {
  &memory_thermal_conf,
  THERMAL_STATUS_NORMAL,
  0
};

static bool blacklisted;


static bool get_memory_temperature(int* temperature)
{
  /* This is where the thermal algorithm for memory temperature would go. */
  /* However, at the moment, there is none; we use omap temp directly. */
  return dsme_omap_get_temperature(temperature);
}


void module_init(module_t* handle)
{
  dsme_log(LOG_DEBUG, "libthermalobject_memory.so loaded");

  blacklisted = dsme_omap_is_blacklisted();
  if (!blacklisted) {
      dsme_register_thermal_object(&memory_thermal_object);
  }
}

void module_fini(void)
{
  if (!blacklisted) {
      dsme_unregister_thermal_object(&memory_thermal_object);
  }

  dsme_log(LOG_DEBUG, "libthermalobject_memory.so unloaded");
}
