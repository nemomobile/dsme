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
#include <time.h>

/* TODO: These should be changed to use statefs once we have it */
#define CORE_SENSOR_MODE "/sys/devices/virtual/thermal/thermal_zone9/mode"
#define CORE_SENSOR_TEMP "/sys/devices/virtual/thermal/thermal_zone9/temp"
#define BATTERY_TEMP     "/sys/class/power_supply/battery/temp"

static int get_current_time(void);

static bool read_temperature(const char *path, int *temperature)
{
    FILE* f;
    bool  ret = false;
    // dsme_log(LOG_DEBUG, "thermal: %s", __FUNCTION__);

    f = fopen(path, "r");
    if (f) {
        if (fscanf(f, "%d", temperature) == 1) {
            // dsme_log(LOG_DEBUG, "thermal: %s = %d C", path, *temperature);
            ret = true;
        }
        fclose(f);
    }
    if (!ret) {
        dsme_log(LOG_DEBUG, "thermal: read of %s FAILED", path);
    }
    return ret;
}

static bool enable_hw_core_temp_sensor(void)
{
    FILE* f; 
    bool ret = false;

    // dsme_log(LOG_DEBUG, "thermal: %s", __FUNCTION__);

    f = fopen(CORE_SENSOR_MODE, "w");
    if (f) {
        if (fprintf(f, "enabled") > 0) {
            ret = true;
        }
        fclose(f);
    }
    if (!ret) {
        dsme_log(LOG_ERR, "FAILED enabling thermal sensor %s", CORE_SENSOR_MODE);
    }
    return ret;
}

extern bool dsme_hw_get_core_temperature(thermal_object_t*         thermal_object,
                                         temperature_handler_fn_t* callback)
{
    int temperature;
    bool got_temperature = false;

    // dsme_log(LOG_DEBUG, "thermal: %s", __FUNCTION__);


    if (read_temperature(CORE_SENSOR_TEMP, &temperature)) {
        got_temperature = true;
    } else {
        /* Read failed, that could be because after deep sleep or transfer
         * between actdead/user state, sensor is disabled and we need to re-enabe it.
         * Try one more time
         */
        dsme_log(LOG_DEBUG, "thermal: First read failed, trying to (re)enable");
        if (enable_hw_core_temp_sensor() &&
            read_temperature(CORE_SENSOR_TEMP, &temperature)) {
            dsme_log(LOG_DEBUG, "thermal: On second try it was ok");
            got_temperature = true;
        }
    }
    if (! got_temperature) {
        dsme_log(LOG_WARNING, "thermal: Can't get core temperature readings");
        temperature = INVALID_TEMPERATURE;
    }
    if (callback)
        callback(thermal_object, temperature);
    return got_temperature;
}

/* Does our HW support this temp reading ? */
extern bool dsme_hw_supports_core_temp(void)
{

    // dsme_log(LOG_DEBUG, "thermal: %s", __FUNCTION__);

    if (enable_hw_core_temp_sensor() &&
        dsme_hw_get_core_temperature(NULL, NULL)) {
        return true;
    } 
    return false;
}

/* Get temp from battery sensor */
/* TODO: This should be changed to use statefs once we have it */

extern bool dsme_hw_get_battery_temperature(thermal_object_t*         thermal_object,
                                            temperature_handler_fn_t* callback)
{
    int temperature;
    bool ret;
    static int previous_temp = -999;
    static int previous_time = 0;
    int current_time = 0;

    // dsme_log(LOG_DEBUG, "thermal: %s", __FUNCTION__);

    if (read_temperature(BATTERY_TEMP, &temperature)) {
        /* We get the temp in one tens */
        temperature = (temperature > 0) ? (temperature + 5 ) / 10 : (temperature - 5 ) / 10;
        ret = true;
        /* Log reading time */
        current_time = get_current_time();
    } else {
        dsme_log(LOG_WARNING, "thermal: Can't get battery temperature readings");
        temperature = INVALID_TEMPERATURE;
        ret = false;
    }

    if (ret) {
        /* Log unsual low or high temp readings */
        if ((temperature < -30) ||
            (temperature > 70)) {
            dsme_log(LOG_WARNING, "thermal: Suspicious battery temperature level %d",temperature);
        }
        if ((previous_temp != -999) &&
            ((temperature > (previous_temp + 4)) ||
             (temperature < (previous_temp - 4)))) {
            /* Battery temp has changed a lot between two readings 
             * Log it as warning if this change happened in short time (~ 2 minutes)
             */
            int delta_time = (current_time > previous_time) ? current_time - previous_time : 0;
            if (delta_time <= 140 ) {
                dsme_log(LOG_WARNING, "thermal: Suspicious battery temperature change from %d to %d in %ds",previous_temp, temperature, delta_time);
            }
        }
        previous_temp = temperature;
        previous_time = current_time;
    }

    if (callback) 
        callback(thermal_object, temperature);
    return ret;
}

/* Does our HW support this temp reading ? */
extern bool dsme_hw_supports_battery_temp(void)
{

    if (dsme_hw_get_battery_temperature(NULL, NULL))
        return true;
    return false;
}

static int get_current_time(void)
{

    struct timespec t;
    int now = 0;

    if (clock_gettime(CLOCK_BOOTTIME, &t) == 0) {
        now = t.tv_sec;
    }
    return now;
}
