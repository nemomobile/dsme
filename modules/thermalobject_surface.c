/**
   @file thermalobject_surface.c

   This file implements a thermal object for tracking
   device surface temperatures.
   <p>
   Copyright (C) 2009-2010 Nokia Corporation

   @author Semi Malinen <semi.malinen@nokia.com>
   @author Matias Muhonen <ext-matias.muhonen@nokia.com>

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
#include "dsme/dsme-wdd-wd.h"

#include "dsme/modules.h"
#include "dsme/logging.h"

#include "dsme_dbus.h"
#include "dbusproxy.h"
#include <dsme/thermalmanager_dbus_if.h>


static bool get_surface_temperature(thermal_object_t*         thermal_object,
                                    temperature_handler_fn_t* callback);

/* thermal limits for device surface */
/*
 * Based on https://projects.maemo.org/bugzilla/show_bug.cgi?id=231167#c9
 * on 2011-04-19:
 */
static thermal_object_configuration_t surface_thermal_conf = {
  "surface",
  {
      /* [min, max], [mintime, maxtime] */
      {    -1,  56,        55,      60 }, /* NORMAL  */
      {    56,  59,        55,      60 }, /* WARNING */
      {    59,  63,        20,      30 }, /* ALERT   */
      {    63,  99,        20,      30 }, /* FATAL   */
  },
  get_surface_temperature
};

static thermal_object_t surface_thermal_object = {
  &surface_thermal_conf,
  THERMAL_STATUS_NORMAL,
  false
};

static temperature_handler_fn_t* handle_temperature          = 0;
static int                       latest_measured_temperature = 0;

static const char* const         service   = thermalmanager_service;
static const char* const         interface = thermalmanager_interface;


static void report_surface_temperature(void* object, int temperature)
{
  /* Thermal algorithm for estimating surface temperature: */
  /*
   * Based on measurement findings brought forth in Thermal measurement
   * results/manager review session on 2009-05-18, the surface temperature
   * can be estimated by subtracting 1 deg C from battery temperature:
   */
  temperature = temperature - 1;

  /* save the temperature for the D-Bus i/f */
  latest_measured_temperature = temperature;

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


static void estimate_surface_temperature(const DsmeDbusMessage* request,
                                         DsmeDbusMessage**      reply)
{
    *reply = dsme_dbus_reply_new(request);
    dsme_dbus_message_append_int(*reply, latest_measured_temperature);
}

static const dsme_dbus_binding_t methods[] = {
    { estimate_surface_temperature, "estimate_surface_temperature" },
    { 0, 0 }
};

static bool bound = false;


DSME_HANDLER(DSM_MSGTYPE_DBUS_CONNECT, client, msg)
{
  dsme_log(LOG_DEBUG, "thermalobject_surface: DBUS_CONNECT");
  dsme_dbus_bind_methods(&bound, methods, service, interface);
}

DSME_HANDLER(DSM_MSGTYPE_DBUS_DISCONNECT, client, msg)
{
  dsme_log(LOG_DEBUG, "thermalobject_surface: DBUS_DISCONNECT");
  dsme_dbus_unbind_methods(&bound, methods, service, interface);
}

module_fn_info_t message_handlers[] = {
  DSME_HANDLER_BINDING(DSM_MSGTYPE_DBUS_CONNECT),
  DSME_HANDLER_BINDING(DSM_MSGTYPE_DBUS_DISCONNECT),
  { 0 }
};

void module_init(module_t* handle)
{
  dsme_log(LOG_DEBUG, "thermalobject_surface.so loaded");

  dsme_register_thermal_object(&surface_thermal_object);
}

void module_fini(void)
{
  dsme_dbus_unbind_methods(&bound, methods, service, interface);

  dsme_unregister_thermal_object(&surface_thermal_object);

  dsme_log(LOG_DEBUG, "thermalobject_surface.so unloaded");
}
