/**
   @file alarmtracker.c

   Track the alarm state from the alarm queue indications sent by the time daemon (timed).
   This is needed for device state selection in the state module;
   if an alarm is set, we go to the acting dead state instead of a reboot or
   shutdown. This allows the device to wake up due to an alarm.

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

/*
 * How to send alarms to alarm tracker:
 * dbus-send --system --type=signal /com/nokia/time com.nokia.time.next_bootup_event int32:0
 * where the int32 parameter is either 0, meaning there are no pending alarms,
 * or the time of the next/current alarm as returned by time(2).
 * Notice that the time may be in the past for current alarms.
 */

#include "dbusproxy.h"
#include "dsme_dbus.h"

#include "dsme/timers.h"
#include "dsme/modules.h"
#include "dsme/logging.h"

#include <dsme/state.h>
#include <dsme/protocol.h>
#include <dsme/alarm_limit.h>

#include <iphbd/iphb_internal.h>

#include <stdio.h>
#include <limits.h>
#include <time.h>
#include <errno.h>
#include <string.h>


/*
 * Store the alarm queue state in a file; it is used to restore the alarm queue state
 * when the module is loaded.
 */
#define ALARM_STATE_FILE     "/var/lib/dsme/alarm_queue_status"
#define ALARM_STATE_FILE_TMP "/var/lib/dsme/alarm_queue_status.tmp"


static time_t alarm_queue_head = 0; /* time of next alarm, or 0 for none */

static bool   alarm_state_file_up_to_date = false;
static bool   external_state_alarm_set    = false;


static dsme_timer_t alarm_state_transition_timer = 0;

static void schedule_next_wakeup(void)
{
    DSM_MSGTYPE_WAIT msg = DSME_MSG_INIT(DSM_MSGTYPE_WAIT);
    msg.req.mintime = 0;
    msg.req.maxtime = msg.req.mintime + 120;
    msg.req.pid     = 0;
    msg.data        = 0;

    broadcast_internally(&msg);
}

static void save_alarm_queue_status(void)
{
  if (alarm_state_file_up_to_date) {
      dsme_log(LOG_DEBUG, "alarmtracker: alarm_state_file_up_to_date");
      return;
   }

  FILE* f;

  if ((f = fopen(ALARM_STATE_FILE_TMP, "w+")) == 0) {
      dsme_log(LOG_DEBUG, "alarmtracker: %s: %s", ALARM_STATE_FILE, strerror(errno));
  } else {
      bool temp_file_ok = true;

      if (fprintf(f, "%ld\n", alarm_queue_head) < 0) {
          dsme_log(LOG_DEBUG, "alarmtracker: Error writing %s", ALARM_STATE_FILE_TMP);
          temp_file_ok = false;
      }

      if (fclose(f) != 0) {
          dsme_log(LOG_DEBUG,
                       "alarmtracker: %s: %s",
                       ALARM_STATE_FILE_TMP,
                       strerror(errno));
          temp_file_ok = false;
      }

      if (temp_file_ok) {
          if (rename(ALARM_STATE_FILE_TMP, ALARM_STATE_FILE) != 0) {
              dsme_log(LOG_DEBUG,
                           "alarmtracker: Error writing file %s",
                           ALARM_STATE_FILE);
          } else {
              alarm_state_file_up_to_date = true;
          }
      }
  }

  if (alarm_state_file_up_to_date) {
      dsme_log(LOG_DEBUG,
      "alarmtracker: Alarm queue head saved to file %s",
      ALARM_STATE_FILE);
  } else {
      dsme_log(LOG_ERR, "alarmtracker: Saving alarm queue head failed");
      /* do not retry to avoid spamming the log */
      alarm_state_file_up_to_date = true;
  }
}

DSME_HANDLER(DSM_MSGTYPE_WAKEUP, client, msg)
{
  save_alarm_queue_status();
}

static void restore_alarm_queue_status(void)
{
  alarm_state_file_up_to_date = false;

  FILE* f;

  if ((f = fopen(ALARM_STATE_FILE, "r")) == 0) {
      dsme_log(LOG_DEBUG, "alarmtracker: %s: %s", ALARM_STATE_FILE, strerror(errno));
  } else {
      if (fscanf(f, "%ld", &alarm_queue_head) != 1) {
          dsme_log(LOG_DEBUG, "alarmtracker: Error reading file %s", ALARM_STATE_FILE);
      } else {
          alarm_state_file_up_to_date = true;
      }

      (void)fclose(f);
  }

  if (alarm_state_file_up_to_date) {
      dsme_log(LOG_DEBUG, "alarmtracker: Alarm queue head restored: %ld", alarm_queue_head);
  } else {
      dsme_log(LOG_WARNING, "alarmtracker: Restoring alarm queue head failed");
  }
}


static int seconds(time_t from, time_t to)
{
  return (to <= 0 || to >= INT_MAX) ? 9999 : (to < from) ? 0 : (int)(to - from);
}

static int set_internal_alarm_state(void* dummy)
{
  static bool alarm_set            = false;

  time_t      now                  = time(0);
  bool        alarm_previously_set = alarm_set;

  /* kill any previous alarm state transition timer */
  if (alarm_state_transition_timer) {
      dsme_destroy_timer(alarm_state_transition_timer);
      alarm_state_transition_timer = 0;
  }

  if (alarm_queue_head != 0) {
      /* there is a queued or active alarm */
      if (alarm_queue_head == 1 || /* 1 means there is an active alarm */
          seconds(now, alarm_queue_head) <= dsme_snooze_timeout_in_seconds())
      {
          /* alarm is either active or soon-to-be-active */
          alarm_set = true;
      } else {
          /* set a timer for transition from not set to soon-to-be-active */
          int transition = seconds(now, alarm_queue_head)
                         - dsme_snooze_timeout_in_seconds();
          alarm_state_transition_timer =
              dsme_create_timer(transition, set_internal_alarm_state, 0);
          dsme_log(LOG_DEBUG, "alarmtracker: next snooze in %d s", transition);

          alarm_set = false;
      }
  } else {
      /* there are no alarms */
      alarm_set = false;
  }

  if (alarm_set != alarm_previously_set) {
      /* inform state module about changed alarm state */
      DSM_MSGTYPE_SET_ALARM_STATE msg =
        DSME_MSG_INIT(DSM_MSGTYPE_SET_ALARM_STATE);

      msg.alarm_set = alarm_set;

      dsme_log(LOG_DEBUG,
               "alarmtracker: broadcasting internal alarm state: %s",
               alarm_set ? "set" : "not set");
      broadcast_internally(&msg);
  }

  return 0; /* stop the interval */
}

static bool upcoming_alarms_exist()
{
    return alarm_queue_head != 0;
}

static void set_external_alarm_state(void)
{
    bool alarm_previously_set = external_state_alarm_set;
    external_state_alarm_set = upcoming_alarms_exist();

    if (external_state_alarm_set != alarm_previously_set) {
        /* inform clients about the change in upcoming alarms */
        DSM_MSGTYPE_SET_ALARM_STATE msg =
            DSME_MSG_INIT(DSM_MSGTYPE_SET_ALARM_STATE);

        msg.alarm_set = external_state_alarm_set;

        dsme_log(LOG_DEBUG,
                 "alarmtracker: broadcasting external alarm state: %s",
                 external_state_alarm_set ? "set" : "not set");
        dsmesock_broadcast(&msg);
    }
}

static void set_alarm_state(void)
{
    set_internal_alarm_state(0);
    set_external_alarm_state();
}


static void alarm_queue_status_ind(const DsmeDbusMessage* ind)
{
    time_t new_alarm_queue_head = dsme_dbus_message_get_int(ind);

    if (!alarm_state_file_up_to_date ||
        new_alarm_queue_head != alarm_queue_head)
    {
        alarm_queue_head = new_alarm_queue_head;

        dsme_log(LOG_DEBUG, "alarmtracker: got new alarm: %ld", alarm_queue_head);

        alarm_state_file_up_to_date = false;
        schedule_next_wakeup();
    } else {
        dsme_log(LOG_DEBUG, "alarmtracker: got old alarm: %ld", alarm_queue_head);
    }

    set_alarm_state();
}


static const dsme_dbus_signal_binding_t signals[] = {
  { alarm_queue_status_ind, "com.nokia.time", "next_bootup_event" },
  { 0, 0 }
};

static bool bound = false;

DSME_HANDLER(DSM_MSGTYPE_DBUS_CONNECT, client, msg)
{
  dsme_log(LOG_DEBUG, "alarmtracker: DBUS_CONNECT");
  dsme_dbus_bind_signals(&bound, signals);
}

DSME_HANDLER(DSM_MSGTYPE_DBUS_DISCONNECT, client, msg)
{
  dsme_log(LOG_DEBUG, "alarmtracker: DBUS_DISCONNECT");
  dsme_dbus_unbind_signals(&bound, signals);
}

DSME_HANDLER(DSM_MSGTYPE_STATE_QUERY, client, req)
{
    DSM_MSGTYPE_SET_ALARM_STATE resp =
        DSME_MSG_INIT(DSM_MSGTYPE_SET_ALARM_STATE);

    resp.alarm_set = external_state_alarm_set;

    endpoint_send(client, &resp);
}

module_fn_info_t message_handlers[] = {
  DSME_HANDLER_BINDING(DSM_MSGTYPE_WAKEUP),
  DSME_HANDLER_BINDING(DSM_MSGTYPE_DBUS_CONNECT),
  DSME_HANDLER_BINDING(DSM_MSGTYPE_DBUS_DISCONNECT),
  DSME_HANDLER_BINDING(DSM_MSGTYPE_STATE_QUERY),
  { 0 }
};


void module_init(module_t* handle)
{
  /* Do not connect to D-Bus; it is probably not started yet.
   * Instead, wait for DSM_MSGTYPE_DBUS_CONNECT.
   */

  dsme_log(LOG_DEBUG, "alarmtracker.so loaded");

  restore_alarm_queue_status();

  set_alarm_state();
}

void module_fini(void)
{
  dsme_dbus_unbind_signals(&bound, signals);

  save_alarm_queue_status();

  dsme_log(LOG_DEBUG, "alarmtracker.so unloaded");
}
