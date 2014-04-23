/**
   @file thermalmanager.c

   This file implements part of the device thermal management policy
   by providing the current thermal state for interested sw components.
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

/*
 * An example command line to obtain thermal state over D-Bus:
 * $ dbus-send --system --print-reply --dest=com.nokia.thermalmanager /com/nokia/thermalmanager com.nokia.thermalmanager.get_thermal_state
 *
 * TODO:
 * - use a single timer for all thermal objects
 *   i.e. use the shortest interval of all thermal objects
 */

#define _GNU_SOURCE

#include "thermalmanager.h"

#include <iphbd/iphb_internal.h>

#include "dbusproxy.h"
#include "dsme_dbus.h"

#include "dsme/modules.h"
#include "dsme/modulebase.h"
#include "dsme/logging.h"
#include "heartbeat.h"

#include <dsme/state.h>
#include <dsme/thermalmanager_dbus_if.h>

#include <glib.h>
#include <stdlib.h>
#include <string.h>

static void receive_temperature_response(thermal_object_t* thermal_object,
                                         int               temperature);
static void thermal_object_polling_interval_expired(void* object);

#ifdef DSME_THERMAL_TUNING
static void thermal_object_try_to_read_config(thermal_object_t* thermal_object);
static thermal_object_t* thermal_object_copy(
  const thermal_object_t* thermal_object);
#endif

#ifdef DSME_THERMAL_LOGGING
static void log_temperature(int temperature, const thermal_object_t* thermal_object);
#endif


static module_t* this_module = 0;

static GSList* thermal_objects = 0;

static const char* const service   = thermalmanager_service;
static const char* const interface = thermalmanager_interface;
static const char* const path      = thermalmanager_path;

static THERMAL_STATUS current_status = THERMAL_STATUS_NORMAL;

#ifdef DSME_THERMAL_TUNING
static bool is_in_ta_test = false;
#endif

static const char* const thermal_status_name[] = {
  "low-temp-warning", "normal", "warning", "alert", "fatal"
};


static const char* current_status_name()
{
  return thermal_status_name[current_status];
}

static THERMAL_STATUS worst_current_thermal_object_status(void)
{
  THERMAL_STATUS overall_status = THERMAL_STATUS_NORMAL;
  THERMAL_STATUS highest_status = THERMAL_STATUS_NORMAL;
  THERMAL_STATUS lowest_status = THERMAL_STATUS_NORMAL;
  GSList*        node;

  for (node = thermal_objects; node != 0; node = g_slist_next(node)) {
      /* Find highest status */
      if (((thermal_object_t*)(node->data))->status > highest_status) {
          highest_status = ((thermal_object_t*)(node->data))->status;
      }
      /* Find lowest status */
      if (((thermal_object_t*)(node->data))->status < lowest_status) {
          lowest_status = ((thermal_object_t*)(node->data))->status;
      }
  }
  /* Decide overall status */
  /* If we have any ALERT of FATAL then that decides overall status */
  /* During LOW, NORMAL or WARNING, any LOW wins */
  /* Else status is highest */
  if (highest_status >= THERMAL_STATUS_ALERT) {
      overall_status = highest_status;
  } else if (lowest_status == THERMAL_STATUS_LOW) {
      overall_status = THERMAL_STATUS_LOW;
  } else {
      overall_status = highest_status;
  }
  return overall_status;
}

static void send_thermal_status(dsme_thermal_status_t status, 
                                const char *sensor_name, int temperature)
{
  DSM_MSGTYPE_SET_THERMAL_STATUS msg =
    DSME_MSG_INIT(DSM_MSGTYPE_SET_THERMAL_STATUS);

  msg.status = status;
  msg.temperature = temperature;
  strncpy(msg.sensor_name, sensor_name, DSM_TEMP_SENSOR_MAX_NAME_LEN); 
  msg.sensor_name[DSM_TEMP_SENSOR_MAX_NAME_LEN - 1] = 0;

  broadcast_internally(&msg);
}

static void send_thermal_indication(const char *sensor_name, int temperature)
{
  /* first send an indication to D-Bus */
  {
      DsmeDbusMessage* sig =
          dsme_dbus_signal_new(path,
                               interface,
                               thermalmanager_state_change_ind);
      dsme_dbus_message_append_string(sig, current_status_name());
      dsme_dbus_signal_emit(sig);
      dsme_log(LOG_NOTICE, "thermalmanager: Device (%s) thermal status: %s (%dC)", sensor_name, current_status_name(), temperature);
  }

  /* then broadcast an indication internally */
  {
      static bool temp_warning_sent = false;

      if (current_status == THERMAL_STATUS_FATAL) {
          send_thermal_status(DSM_THERMAL_STATUS_OVERHEATED, sensor_name, temperature);
          temp_warning_sent = true;
          dsme_log(LOG_CRIT, "thermalmanager: Device (%s) overheated (%dC)", sensor_name, temperature);
      } else if (current_status == THERMAL_STATUS_LOW) {
          send_thermal_status(DSM_THERMAL_STATUS_LOWTEMP, sensor_name, temperature);
          temp_warning_sent = true;
          dsme_log(LOG_WARNING, "thermalmanager: Device (%s) temperature low (%dC)", sensor_name, temperature);
      } else if (temp_warning_sent) {
          send_thermal_status(DSM_THERMAL_STATUS_NORMAL, sensor_name, temperature);
          temp_warning_sent = false;
          dsme_log(LOG_NOTICE, "thermalmanager: Device (%s) temperature back to normal (%dC)", sensor_name, temperature);
      }
  }
}

static void send_temperature_request(thermal_object_t* thermal_object)
{
  if (!thermal_object->request_pending) {
      dsme_log(LOG_DEBUG,
               "thermalmanager: requesting %s temperature",
               thermal_object->conf->name);
      thermal_object->request_pending = true;
      if (!thermal_object->conf->request_temperature(
               thermal_object,
               receive_temperature_response))
      {
          thermal_object->request_pending = false;
          dsme_log(LOG_DEBUG,
                   "thermalmanager: error requesting %s temperature",
                   thermal_object->conf->name);
      }
  } else {
      dsme_log(LOG_DEBUG,
               "thermalmanager: still waiting for %s temperature",
               thermal_object->conf->name);
  }
}

static void receive_temperature_response(thermal_object_t* thermal_object,
                                         int               temperature)
{
  thermal_object->request_pending = false;

  if (temperature == INVALID_TEMPERATURE) {
      dsme_log(LOG_DEBUG,
               "thermalmanager: %s temperature request failed",
               thermal_object->conf->name);
      return;
  }

  THERMAL_STATUS previous_status = thermal_object->status;
  THERMAL_STATUS new_status      = thermal_object->status;

#ifdef DSME_THERMAL_TUNING
  if (is_in_ta_test) {
      thermal_object_try_to_read_config(thermal_object);
  }
#endif

  /* We require that temp readings are C degrees integers
   * and we drop obviously wrong values
   */
  if ((temperature > 200) || (temperature < -50)) { 
      dsme_log(LOG_WARNING,
               "thermalmanager: invalid temperature reading: %s %dC",
               thermal_object->conf->name,
               temperature);
      return;
  }

  /* figure out the new thermal object status based on the temperature */
  if (temperature < thermal_object->conf->state[new_status].min) {
      while (new_status > THERMAL_STATUS_LOW &&
             temperature < thermal_object->conf->state[new_status].min)
      {
          --new_status;
      }
  } else if (temperature > thermal_object->conf->state[new_status].max) {
      while (new_status < THERMAL_STATUS_FATAL &&
             temperature > thermal_object->conf->state[new_status].max)
      {
          ++new_status;
      }
  }

  dsme_log(LOG_DEBUG,
           "thermalmanager: %s temperature: %d %s",
           thermal_object->conf->name,
           temperature,
           thermal_status_name[new_status]);

  if ((new_status != previous_status) || thermal_object->status_change_count) {
      /* Thermal object status has changed, but it can be because of bad reading. 
       * Before accepting new status, make sure it is not a glitch.
       * We want 3 consecutive readings indicating same status before we change it.
       */
      if (thermal_object->status_change_count == 0) {
          /* This is first reading for new status */
          thermal_object->status_change_count = 1;
          thermal_object->new_status = new_status;
          dsme_log(LOG_DEBUG,
                   "thermalmanager: New status in transition started");
      } else {
          /* We are in transition. Make sure all new readings want same status */
          if (thermal_object->new_status != new_status) {
              thermal_object->status_change_count = 0;
              dsme_log(LOG_DEBUG,
                       "thermalmanager: New status in transition did not remain same");
          } else if (++(thermal_object->status_change_count) >= 3) {
              /* We got 3 readings all indicating new status, better believe it */
              dsme_log(LOG_DEBUG,
                       "thermalmanager: New status accepted: %s", thermal_status_name[new_status]);
              thermal_object->status = new_status;
              thermal_object->status_change_count = 0;
              /* see if the new status affects global thermal status */
              THERMAL_STATUS previously_indicated_status = current_status;
              current_status = worst_current_thermal_object_status();
              if (current_status != previously_indicated_status) {
                  /* global thermal status has changed; send indication */
                  send_thermal_indication(thermal_object->conf->name, temperature);
              }
          }
      }
  }

#ifdef DSME_THERMAL_LOGGING
  log_temperature(temperature, thermal_object);
#endif
}

static void thermal_object_polling_interval_expired(void* object)
{
  thermal_object_t* thermal_object = object;

  send_temperature_request(thermal_object);

  // set up heartbeat service to poll the temperature of the object
  const thermal_status_configuration_t* conf =
      &thermal_object->conf->state[thermal_object->status];

  DSM_MSGTYPE_WAIT msg = DSME_MSG_INIT(DSM_MSGTYPE_WAIT);
  /* If thermal status is in transition and we are taking several
   * readings before change, then use speed up values for
   * next readings. Otherwise use configured values
   */
  if (thermal_object->status_change_count) {
      msg.req.mintime = 2;
      msg.req.maxtime = 5;
  } else {
      msg.req.mintime = conf->mintime;
      msg.req.maxtime = conf->maxtime;
  }
  msg.req.pid     = 0;
  msg.data        = thermal_object;

  broadcast_internally(&msg);
}


void dsme_register_thermal_object(thermal_object_t* thermal_object)
{
    dsme_log(LOG_DEBUG,
             "thermalmanager: %s (%s)", __FUNCTION__,
             thermal_object->conf->name);

  enter_module(this_module);

#ifdef DSME_THERMAL_TUNING
  thermal_object = thermal_object_copy(thermal_object);
  thermal_object_try_to_read_config(thermal_object);
#endif

  // add the thermal object to the list of know thermal objects
  thermal_objects = g_slist_append(thermal_objects, thermal_object);

  thermal_object_polling_interval_expired(thermal_object);

  leave_module();
}

void dsme_unregister_thermal_object(thermal_object_t* thermal_object)
{
  dsme_log(LOG_DEBUG, "thermalmanager: %s(%s)", __FUNCTION__, thermal_object->conf->name);
  // TODO
}


static void get_thermal_state(const DsmeDbusMessage* request,
                              DsmeDbusMessage**      reply)
{
  *reply = dsme_dbus_reply_new(request);
  dsme_dbus_message_append_string(*reply, current_status_name());
}

static const dsme_dbus_binding_t methods[] = {
  { get_thermal_state, thermalmanager_get_thermal_state },
  { 0, 0 }
};

static bool bound = false;


DSME_HANDLER(DSM_MSGTYPE_WAKEUP, client, msg)
{
    thermal_object_t* thermal_object = (thermal_object_t*)(msg->data);

    dsme_log(LOG_DEBUG,
             "thermalmanager: check thermal object '%s'",
             thermal_object->conf->name);
    thermal_object_polling_interval_expired(thermal_object);
}


DSME_HANDLER(DSM_MSGTYPE_DBUS_CONNECT, client, msg)
{
  dsme_log(LOG_DEBUG, "thermalmanager: DBUS_CONNECT");
  dsme_dbus_bind_methods(&bound, methods, service, interface);
}

DSME_HANDLER(DSM_MSGTYPE_DBUS_DISCONNECT, client, msg)
{
  dsme_log(LOG_DEBUG, "thermalmanager: DBUS_DISCONNECT");
  dsme_dbus_unbind_methods(&bound, methods, service, interface);
}

#ifdef DSME_THERMAL_TUNING
DSME_HANDLER(DSM_MSGTYPE_SET_TA_TEST_MODE, client, msg)
{
    is_in_ta_test = true;
    dsme_log(LOG_NOTICE, "thermalmanager: set TA test mode");
}
#endif


// TODO: rename module_fn_info_t to dsme_binding_t
module_fn_info_t message_handlers[] = {
  DSME_HANDLER_BINDING(DSM_MSGTYPE_WAKEUP),
  DSME_HANDLER_BINDING(DSM_MSGTYPE_DBUS_CONNECT),
  DSME_HANDLER_BINDING(DSM_MSGTYPE_DBUS_DISCONNECT),
#ifdef DSME_THERMAL_TUNING
  DSME_HANDLER_BINDING(DSM_MSGTYPE_SET_TA_TEST_MODE),
#endif
  { 0 }
};


void module_init(module_t* handle)
{
  dsme_log(LOG_DEBUG, "thermalmanager.so loaded");

  this_module = handle;
}

void module_fini(void)
{
  g_slist_free(thermal_objects);

  dsme_dbus_unbind_methods(&bound, methods, service, interface);

  dsme_log(LOG_DEBUG, "thermalmanager.so unloaded");
}

#ifdef DSME_THERMAL_TUNING
#include <stdio.h>

/* Thermal values can be configured in file /etc/dsme/temp_<name>.conf */
#define DSME_THERMAL_TUNING_CONF_PATH "/etc/dsme/temp_"

static FILE* thermal_tuning_file(const char* thermal_object_name)
{
  char  name[1024];

  snprintf(name,
           sizeof(name),
           "%s%s.conf",
           DSME_THERMAL_TUNING_CONF_PATH,
           thermal_object_name);

  dsme_log(LOG_DEBUG, "thermalmanager: trying to open %s for thermal tuning values", name);

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
                 &new_config.state[i].maxtime) != 3) {
          success = false;
      }
      if (success) {
          /* Do some sanity checking for values 
           * Temp values should be between -40..+200, and in ascending order.
           * Min must be < max
           * Next min <= previous max
           * Polling times should also make sense  10-1000s
           */
          if (((i > THERMAL_STATUS_LOW) && (new_config.state[i].min < -40)) ||
              (new_config.state[i].max < -40) ||
              (new_config.state[i].min > 200) ||
              ((i > THERMAL_STATUS_FATAL) && (new_config.state[i].max > 200)) ||
              (new_config.state[i].min >= new_config.state[i].max) ||
              ((i > THERMAL_STATUS_LOW) && (new_config.state[i].min <= new_config.state[i-1].min)) ||
              ((i > THERMAL_STATUS_LOW) && (new_config.state[i].max <= new_config.state[i-1].max)) ||
              ((i > THERMAL_STATUS_LOW) && (new_config.state[i].min > new_config.state[i-1].max)) ||
              (new_config.state[i].maxtime < 10) ||
              (new_config.state[i].maxtime > 1000)) {
              success = false;
          }
      }
      if (success) {
          /* Note, it is important to give big enough window min..max
           * then IPHB can freely choose best wake-up time
           */
          new_config.state[i].mintime = new_config.state[i].maxtime/2;
      } else {
          dsme_log(LOG_ERR, "thermalmanager: syntax error in thermal tuning on line %d", i+1);
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
          dsme_log(LOG_NOTICE,
                   "thermalmanager: Read thermal tuning file for %s",
                   thermal_object->conf->name);
      } else {
          dsme_log(LOG_NOTICE,
                   "thermalmanager: Thermal tuning file for %s discarded. Using default values",
                   thermal_object->conf->name);
      }

      fclose(f);
  } else {
      dsme_log(LOG_NOTICE,
               "thermalmanager: No thermal tuning file for %s. Using default values",
               thermal_object->conf->name);
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
  case THERMAL_STATUS_LOW:     return "LOW_WARNING";
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
                   "thermalmanager: Error opening thermal log " DSME_THERMAL_LOG_PATH ": %s",
                   strerror(errno));
          return;
      }
  }

  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);

  int now = t.tv_sec;
  static int start_time = 0;

  if (!start_time) {
      start_time = now;
  }

  fprintf(log_file,
          "%d %s %d C %s\n",
          now - start_time,
          thermal_object->conf->name,
          temperature,
          status_string(thermal_object->status));
  fflush(log_file);
  dsme_log(LOG_DEBUG,"thermalmanager: %s %d C %s", thermal_object->conf->name, 
           temperature, status_string(thermal_object->status));
}
#endif
