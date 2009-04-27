/**
   @file thermalmanager.c

   This file implements part of the device thermal management policy
   by providing the current thermal state for interested sw components.
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

/*
 * An example command line to obtain thermal state over D-Bus:
 * $ dbus-send --system --print-reply --dest=com.nokia.thermalmanager /com/nokia/thermalmanager com.nokia.thermalmanager.get_thermal_state
 *
 * TODO:
 * - use a single timer for all thermal objects
 *   i.e. use the shortest interval of all thermal objects
 */
#include "thermalmanager.h"

#include "dbusproxy.h"
#include "dsme_dbus.h"

#include "dsme/modules.h"
#include "dsme/logging.h"
#include "state.h"

#include <glib.h>
#include <stdlib.h>


static int thermal_object_polling_interval_expired(void* object);

#ifdef DSME_THERMAL_TUNING
static void thermal_object_try_to_read_config(thermal_object_t* thermal_object);
static thermal_object_t* thermal_object_copy(
  const thermal_object_t* thermal_object);
#endif

#ifdef DSME_THERMAL_LOGGING
static void log_temperature(int temperature, const thermal_object_t* thermal_object);
#endif


static GSList* thermal_objects = 0;

static const char* const service   = "com.nokia.thermalmanager";
static const char* const interface = "com.nokia.thermalmanager";
static const char* const path      = "/com/nokia/thermalmanager";

static THERMAL_STATUS current_status = THERMAL_STATUS_NORMAL;


static const char* current_status_name()
{
  static const char* const thermal_status_name[] = {
      "normal", "warning", "alert", "fatal"
  };

  return thermal_status_name[current_status];
}

static THERMAL_STATUS worst_current_thermal_object_status(void)
{
  THERMAL_STATUS status = THERMAL_STATUS_NORMAL;
  GSList*        node;

  for (node = thermal_objects; node != 0; node = g_slist_next(node)) {
      if (((thermal_object_t*)(node->data))->status > status) {
          status = ((thermal_object_t*)(node->data))->status;
      }
  }

  return status;
}

static void send_overheat_status(bool overheated)
{
  DSM_MSGTYPE_SET_THERMAL_STATE msg =
    DSME_MSG_INIT(DSM_MSGTYPE_SET_THERMAL_STATE);

  msg.overheated = overheated;

  broadcast_internally(&msg);
}

static void send_thermal_indication(void)
{
  /* first send an indication to D-Bus */
  {
      DsmeDbusMessage* sig =
          dsme_dbus_signal_new(path, interface, "thermal_state_change_ind");
      dsme_dbus_message_append_string(sig, current_status_name());
      dsme_dbus_signal_emit(sig);
      dsme_log(LOG_INFO, "thermal status: %s", current_status_name());
  }

  /* then broadcast an indication internally */
  {
      static bool overheated = false;

      if (current_status == THERMAL_STATUS_FATAL) {
          send_overheat_status(true);
          overheated = true;
          dsme_log(LOG_CRIT, "Device overheated");
      } else if (overheated) {
          send_overheat_status(false);
          overheated = false;
          dsme_log(LOG_CRIT, "Device no longer overheated");
      }
  }
}

static void update_thermal_object_status(thermal_object_t* thermal_object)
{
  THERMAL_STATUS previous_status = thermal_object->status;
  THERMAL_STATUS new_status      = thermal_object->status;
  int            temp            = 0;

#ifdef DSME_THERMAL_TUNING
  thermal_object_try_to_read_config(thermal_object);
#endif

  if (!thermal_object->conf->get_temperature(&temp)) {
      dsme_log(LOG_CRIT,
               "error getting %s temperature",
               thermal_object->conf->name);
      // TODO: WHAT IS THE SENSIBLE THING TO DO IF A SENSOR FAILS?
      return;
  }

  if (temp > 1000) {
      /* convert from millidegrees to degrees */
      temp = temp / 1000;
  }

  if (temp > 250) {
      /* convert from kelvin to degrees celsius */
      temp = temp - 273;
  }

#ifndef DSME_THERMAL_LOGGING
  dsme_log(LOG_DEBUG, "%s temperature: %d", thermal_object->conf->name, temp);
#endif

  /* figure out the new thermal object status based on the temperature */
  if        (temp < thermal_object->conf->state[new_status].min) {
      while (new_status > THERMAL_STATUS_NORMAL &&
             temp < thermal_object->conf->state[new_status].min)
      {
          --new_status;
      }
  } else if (temp > thermal_object->conf->state[new_status].max) {
      while (new_status < THERMAL_STATUS_FATAL &&
             temp > thermal_object->conf->state[new_status].max)
      {
          ++new_status;
      }
  }
  thermal_object->status = new_status;

  if (new_status != previous_status) {
      /* thermal object status has changed; see if it affects thermal status */

      THERMAL_STATUS previously_indicated_status = current_status;
      current_status = worst_current_thermal_object_status();

      if (current_status != previously_indicated_status) {
          /* thermal status has changed; send indication */
          send_thermal_indication();
      }
  }

#ifdef DSME_THERMAL_LOGGING
  log_temperature(temp, thermal_object);
#endif
}

static void start_thermal_object_polling_interval(
  thermal_object_t* thermal_object)
{
  unsigned interval;

#ifdef DSME_THERMAL_LOGGING
  interval = 1; // always poll at 1 s interval when logging
#else
  interval = thermal_object->conf->state[thermal_object->status].interval;
#endif

  thermal_object->timer =
      dsme_create_timer(interval,
                        thermal_object_polling_interval_expired,
                        thermal_object);
  if (!thermal_object->timer) {
      dsme_log(LOG_CRIT, "Unable to create a timer for thermal object");
      exit(EXIT_FAILURE);
  }
}

static int thermal_object_polling_interval_expired(void* object)
{
  thermal_object_t* thermal_object = object;

  THERMAL_STATUS previous_status = thermal_object->status;
  bool           keep_interval   = true;

  update_thermal_object_status(thermal_object);

  if (thermal_object->status != previous_status) {
      /* thermal object's status has changed; see if interval has changed */
      if (thermal_object->conf->state[previous_status].interval !=
          thermal_object->conf->state[thermal_object->status].interval)
      {
          /* new status has a different polling interval; adopt it */
          start_thermal_object_polling_interval(thermal_object);

          keep_interval = false; /* stop the previous interval */
      }
  }

  return keep_interval;
}

void dsme_register_thermal_object(thermal_object_t* thermal_object)
{
#ifdef DSME_THERMAL_TUNING
  thermal_object = thermal_object_copy(thermal_object);
#endif

  thermal_objects = g_slist_append(thermal_objects, thermal_object);

  /* start polling the new thermal object */
  update_thermal_object_status(thermal_object);
  start_thermal_object_polling_interval(thermal_object);
}

void dsme_unregister_thermal_object(thermal_object_t* thermal_object)
{
  // TODO
}


static void get_thermal_state(const DsmeDbusMessage* request,
                              DsmeDbusMessage**      reply)
{
  *reply = dsme_dbus_reply_new(request);
  dsme_dbus_message_append_string(*reply, current_status_name());
}

static const dsme_dbus_binding_t methods[] = {
  { get_thermal_state, "get_thermal_state" },
  { 0, 0 }
};

static bool bound = false;


DSME_HANDLER(DSM_MSGTYPE_DBUS_CONNECT, client, msg)
{
  dsme_dbus_bind_methods(&bound, methods, service, interface);
}

DSME_HANDLER(DSM_MSGTYPE_DBUS_DISCONNECT, client, msg)
{
  dsme_dbus_unbind_methods(&bound, methods, service, interface);
}

// TODO: rename module_fn_info_t to dsme_binding_t
module_fn_info_t message_handlers[] = {
  DSME_HANDLER_BINDING(DSM_MSGTYPE_DBUS_CONNECT),
  DSME_HANDLER_BINDING(DSM_MSGTYPE_DBUS_DISCONNECT),
  { 0 }
};


void module_init(module_t* handle)
{
  dsme_log(LOG_DEBUG, "libthermalmanager.so loaded");
}

void module_fini(void)
{
  GSList* node;

  /* stop thermal object polling timers */
  for (node = thermal_objects; node != 0; node = g_slist_next(node)) {
      if (((thermal_object_t*)(node->data))->timer) {
          dsme_destroy_timer(((thermal_object_t*)(node->data))->timer);
          ((thermal_object_t*)(node->data))->timer = 0;
      }
  }

  g_slist_free(thermal_objects);

  dsme_dbus_unbind_methods(&bound, methods, service, interface);

  dsme_log(LOG_DEBUG, "libthermalmanager.so unloaded");
}

#ifdef DSME_THERMAL_TUNING
#include <stdio.h>

#define DSME_THERMAL_TUNING_CONF_PATH "/etc/dsme/temp_"

static FILE* thermal_tuning_file(const char* thermal_object_name)
{
  char  name[1024];

  snprintf(name,
           sizeof(name),
           "%s%s",
           DSME_THERMAL_TUNING_CONF_PATH,
           thermal_object_name);

#ifndef DSME_THERMAL_LOGGING
  dsme_log(LOG_INFO, "trying to open %s for thermal tuning values", name);
#endif

  return fopen(name, "r");
}

static bool thermal_object_config_read(
  thermal_object_configuration_t* config,
  FILE*                           f)
{
  bool                           success = true;
  int                            i;
  thermal_object_configuration_t new_config;

  new_config = *config;

  for (i = 0; i < THERMAL_STATUS_COUNT; ++i) {
      if (fscanf(f,
                 "%d, %d, %d",
                 &new_config.state[i].min,
                 &new_config.state[i].max,
                 &new_config.state[i].interval) != 3)
      {
          dsme_log(LOG_CRIT, "syntax error in thermal tuning on line %d", i+1);
          success = false;
          break;
      }
  }

  if (success) {
      *config = new_config;
  }

  return success;
}

static void thermal_object_try_to_read_config(thermal_object_t* thermal_object)
{
  FILE* f;

  if ((f = thermal_tuning_file(thermal_object->conf->name))) {

      if (thermal_object_config_read(thermal_object->conf, f)) {
          dsme_log(LOG_INFO,
                   "(re)read thermal tuning file for %s;"
                   " thermal values may have changed",
                   thermal_object->conf->name);
      } else {
          dsme_log(LOG_INFO,
                   "thermal tuning file for %s discarded;"
                   " no change in thermal values",
                   thermal_object->conf->name);
      }

      fclose(f);
#ifndef DSME_THERMAL_LOGGING
  } else {
      dsme_log(LOG_INFO,
               "no thermal tuning file for %s; no change in thermal values",
               thermal_object->conf->name);
#endif
  }
}

static thermal_object_t* thermal_object_copy(
  const thermal_object_t* thermal_object)
{
  thermal_object_t*               copy_object;
  thermal_object_configuration_t* copy_config;

  copy_object = malloc(sizeof(thermal_object_t));
  copy_config = malloc(sizeof(thermal_object_configuration_t));

  if (copy_object && copy_config) {
      *copy_object = *thermal_object;
      *copy_config = *(thermal_object->conf);
      copy_object->conf = copy_config;
  } else {
      free(copy_object);
      free(copy_config);

      copy_object = 0;
  }

  return copy_object;
}
#endif

#ifdef DSME_THERMAL_LOGGING
#include <stdio.h>
#include <string.h>
#include <errno.h>

#define DSME_THERMAL_LOG_PATH "/var/lib/dsme/thermal.log"

static const char* status_string(THERMAL_STATUS status)
{
  switch (status) {
  case THERMAL_STATUS_NORMAL:  return "NORMAL";
  case THERMAL_STATUS_WARNING: return "WARNING";
  case THERMAL_STATUS_ALERT:   return "ALERT";
  case THERMAL_STATUS_FATAL:   return "FATAL";
  default:                     return "UNKNOWN";
  }
}

static void log_temperature(int temperature, const thermal_object_t* thermal_object)
{
  static FILE* log_file = 0;

  if (!log_file) {
      if (!(log_file = fopen(DSME_THERMAL_LOG_PATH, "a"))) {
          dsme_log(LOG_ERR,
                   "Error opening thermal log " DSME_THERMAL_LOG_PATH ": %s",
                   strerror(errno));
          return;
      }
  }

  fprintf(log_file,
          "%d %d %s\n",
          (int)time(0),
          temperature,
          status_string(thermal_object->status));
  fflush(log_file);
}
#endif
