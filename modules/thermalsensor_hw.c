/**
   @file thermalsensor_hw.c

   This module provides HW temperature readings.
   <p>
   Copyright (C) 2013 Jolla ltd

   @author Pekka Lundstrom <pekka.lundstrom@jolla.com>

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
#include "thermalmanager.h"
#include "thermalsensor_hw.h"
#include "dsme/logging.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>


static bool read_temperature(const char *path, int *temperature)
{
    FILE* fd; 
    bool  ret = false;
    // dsme_log(LOG_DEBUG, "thermal: %s", __FUNCTION__);

    fd = fopen(path, "r");
    if (fd) {
        if (fscanf(fd, "%d", temperature) == 1) {
            // dsme_log(LOG_DEBUG, "thermal: %s = %d C", path, *temperature);
            ret = true;
        }
        fclose(fd);
    } else {
        dsme_log(LOG_DEBUG, "thermal: read of %s FAILED", path);
    }
    return ret;
}

/* Get temp from sensor 9 */
/* TODO: This should be changed to use statefs once we have it */

#define CORE_SENSOR_MODE "/sys/devices/virtual/thermal/thermal_zone9/mode"
#define CORE_SENSOR_TEMP "/sys/devices/virtual/thermal/thermal_zone9/temp"

extern bool dsme_hw_get_core_temperature(thermal_object_t*         thermal_object,
                                         temperature_handler_fn_t* callback)
{
    int temperature;

    // dsme_log(LOG_DEBUG, "thermal: %s", __FUNCTION__);

    if (read_temperature(CORE_SENSOR_TEMP, &temperature)) {
        if (callback) 
            callback(thermal_object, temperature);
        return true;
    } else {
        return false;
    }
}

static bool enable_hw_core_temp_sensor(void)
{
    FILE* fd; 
    bool ret = false;

    // dsme_log(LOG_DEBUG, "thermal: %s", __FUNCTION__);

    fd = fopen(CORE_SENSOR_MODE, "w");
    if (fd) {
        if (fprintf(fd, "enabled") > 0) {
            ret = true;
        }
        fclose(fd);
    } else {
        dsme_log(LOG_ERR, "FAILED enabling thermal sensor %s", CORE_SENSOR_MODE);
    }
    return ret;
}

/* Does our HW support this temp reading ? */
extern bool dsme_hw_supports_core_temp(void)
{

    // dsme_log(LOG_DEBUG, "thermal: %s", __FUNCTION__);

    if (enable_hw_core_temp_sensor() &&
        dsme_hw_get_core_temperature(NULL, NULL)) {
        return true;
    } else {
        return false;
    }
}

/* Get temp from battery sensor */
/* TODO: This should be changed to use statefs once we have it */

extern bool dsme_hw_get_battery_temperature(thermal_object_t*         thermal_object,
                                            temperature_handler_fn_t* callback)
{
    int temperature;

    // dsme_log(LOG_DEBUG, "thermal: %s", __FUNCTION__);

    if (read_temperature("/sys/class/power_supply/battery/temp", &temperature)) {
      /* We get the temp in one tens */
      temperature = (temperature + 5 ) / 10;
        if (callback) 
            callback(thermal_object, temperature);
        return true;
    } else {
        return false;
    }
}

/* Does our HW support this temp reading ? */
extern bool dsme_hw_supports_battery_temp(void)
{

    if (dsme_hw_get_battery_temperature(NULL, NULL))
        return true;
    else
        return false;
}
