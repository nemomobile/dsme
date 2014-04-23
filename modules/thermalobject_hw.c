/**
   @file thermalobject_hw.c

   This file implements a thermal object for tracking HW temperatures.
   Battery and SOC core temperatures
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

#include "dsme/modules.h"
#include "dsme/logging.h"

#include "dsme_dbus.h"
#include "dbusproxy.h"
#include <dsme/thermalmanager_dbus_if.h>

static bool core_registered = false;
static bool battery_registered = false;


/* These are default values that can be overridden by
   HW adaptation file /etc/dsme/temp_hw_core
*/
static thermal_object_configuration_t core_thermal_conf = {
  "hw_core",
  {
      /* (min, max], [mintime, maxtime] */
      {   -99, -14,        60,     120 }, /* LOWTEMP */
      {   -15, 100,        60,     120 }, /* NORMAL  */
      {    99, 110,        30,      60 }, /* WARNING */
      {   109, 119,         5,      10 }, /* ALERT   */
      {   119, 200,         5,      10 }  /* FATAL   */
  },
  dsme_hw_get_core_temperature
};

static thermal_object_t core_thermal_object = {
  &core_thermal_conf,
  THERMAL_STATUS_NORMAL,
  THERMAL_STATUS_NORMAL,
  0,
  0
};

/* These are default values that can be overridden by
   HW adaptation file /etc/dsme/temp_hw_battery
*/
static thermal_object_configuration_t battery_thermal_conf = {
  "hw_battery",
  {
      /* (min, max], [mintime, maxtime] */
      {   -99,  -3,        60,     120 }, /* LOWTEMP */
      {    -4,  60,        60,     120 }, /* NORMAL  */
      {    59,  65,        30,      60 }, /* WARNING */
      {    64,  68,         5,      10 }, /* ALERT   */
      {    68,  99,         5,      10 }, /* FATAL   */
  },
  dsme_hw_get_battery_temperature
};

static thermal_object_t battery_thermal_object = {
  &battery_thermal_conf,
  THERMAL_STATUS_NORMAL,
  THERMAL_STATUS_NORMAL,
  0,
  0
};

static int measured_core_temp = INVALID_TEMPERATURE;
static int measured_battery_temp = INVALID_TEMPERATURE;

static void core_temp_cb(thermal_object_t* thermal_object, int temp)
{
    (void) thermal_object;
    measured_core_temp = temp;
}

static void battery_temp_cb(thermal_object_t* thermal_object, int temp)
{
    (void) thermal_object;
    measured_battery_temp = temp;
}

static void core_temperature(const DsmeDbusMessage* request,
                             DsmeDbusMessage**      reply)
{
    dsme_hw_get_core_temperature(NULL, core_temp_cb);
    *reply = dsme_dbus_reply_new(request);
    dsme_dbus_message_append_int(*reply, measured_core_temp);
}

static void battery_temperature(const DsmeDbusMessage* request,
                                DsmeDbusMessage**      reply)
{
    dsme_hw_get_battery_temperature(NULL, battery_temp_cb);
    *reply = dsme_dbus_reply_new(request);
    dsme_dbus_message_append_int(*reply, measured_battery_temp);
}

static const dsme_dbus_binding_t methods[] = {
    { core_temperature, "core_temperature" },
    { battery_temperature, "battery_temperature" },
    { 0, 0 }
};

static bool bound = false;
static const char* const service   = thermalmanager_service;
static const char* const interface = thermalmanager_interface;

DSME_HANDLER(DSM_MSGTYPE_DBUS_CONNECT, client, msg)
{
  dsme_log(LOG_DEBUG, "thermalobject_hw: DBUS_CONNECT");
  dsme_dbus_bind_methods(&bound, methods, service, interface);
}

DSME_HANDLER(DSM_MSGTYPE_DBUS_DISCONNECT, client, msg)
{
  dsme_log(LOG_DEBUG, "thermalobject_hw: DBUS_DISCONNECT");
  dsme_dbus_unbind_methods(&bound, methods, service, interface);
}

module_fn_info_t message_handlers[] = {
  DSME_HANDLER_BINDING(DSM_MSGTYPE_DBUS_CONNECT),
  DSME_HANDLER_BINDING(DSM_MSGTYPE_DBUS_DISCONNECT),
  { 0 }
};

void module_init(module_t* handle)
{
    dsme_log(LOG_DEBUG, "thermalobject_hw.so loaded");
    if (dsme_hw_supports_core_temp()) {
        dsme_register_thermal_object(&core_thermal_object);
        core_registered = true;
    } else {
        dsme_log(LOG_NOTICE, "thermalobject_hw_core: No HW support, going to sleep only mode");
    }
    if (dsme_hw_supports_battery_temp()) {
        dsme_register_thermal_object(&battery_thermal_object);
        battery_registered = true;
    } else {
        dsme_log(LOG_NOTICE, "thermalobject_hw_battery: No HW support, going to sleep only mode");
    }
}

void module_fini(void)
{
    if (core_registered) {
        dsme_unregister_thermal_object(&core_thermal_object);
        core_registered = false;
    }
    if (battery_registered) {
        dsme_unregister_thermal_object(&battery_thermal_object);
        battery_registered = false;
    }
    dsme_log(LOG_DEBUG, "thermalobject_hw.so unloaded");
}
