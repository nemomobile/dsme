/**
   @file alarmtracker.c

   Track the alarm state from the alarm queue indications sent by alarmd.
   This is needed for device state selection by the state module.
   <p>
   Copyright (C) 2009 Nokia Corporation.

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
 * dbus-send --system --type=signal /com/nokia/alarmd com.nokia.alarmd.queue_status_ind int32:0 int32:1 int32:2 int32:3
 */

#include "dbusproxy.h"
#include "dsme_dbus.h"

#include "dsme/timers.h"
#include "dsme/modules.h"
#include "dsme/logging.h"

#include <dsme/state.h>

#include <stdio.h>
#include <limits.h>
#include <time.h>
#include <errno.h>
#include <string.h>


#define SNOOZE_TIMEOUT 120 /* 2 minutes */

#define ALARM_STATE_FILE     "/var/lib/dsme/alarm_queue_status"
#define ALARM_STATE_FILE_TMP "/var/lib/dsme/alarm_queue_status.tmp"


/* alarm queue head */
static time_t active  = 0;       /* # of active alarms */
static time_t desktop = INT_MAX; /* next desktop alarm */
static time_t actdead = INT_MAX; /* next actdead alarm */

static bool   alarm_state_file_up_to_date = false;

static dsme_timer_t alarm_state_transition_timer = 0;


static void save_alarm_queue_status_cb(void)
{
  if (alarm_state_file_up_to_date) {
      return;
   }

  FILE* f;

  if ((f = fopen(ALARM_STATE_FILE_TMP, "w+")) == 0) {
      dsme_log_raw(LOG_DEBUG, "%s: %s", ALARM_STATE_FILE, strerror(errno));
  } else {
      bool temp_file_ok = true;

      if (fprintf(f, "%ld, %ld, %ld\n", active, desktop, actdead) < 0) {
          dsme_log_raw(LOG_DEBUG, "Error writing %s", ALARM_STATE_FILE_TMP);
          temp_file_ok = false;
      }

      if (fclose(f) != 0) {
          dsme_log_raw(LOG_DEBUG,
                       "%s: %s",
                       ALARM_STATE_FILE_TMP,
                       strerror(errno));
          temp_file_ok = false;
      }

      if (temp_file_ok) {
          if (rename(ALARM_STATE_FILE_TMP, ALARM_STATE_FILE) != 0) {
              dsme_log_raw(LOG_DEBUG,
                           "Error writing file %s",
                           ALARM_STATE_FILE);
          } else {
              alarm_state_file_up_to_date = true;
          }
      }
  }

  if (alarm_state_file_up_to_date) {
      dsme_log_raw(LOG_DEBUG,
      "Alarm queue status saved to file %s",
      ALARM_STATE_FILE);
  } else {
      dsme_log_raw(LOG_ERR, "Saving alarm queue status failed");
      /* do not retry to avoid spamming the log */
      alarm_state_file_up_to_date = true;
  }
}

static void restore_alarm_queue_status(void)
{
  alarm_state_file_up_to_date = false;

  FILE* f;

  if ((f = fopen(ALARM_STATE_FILE, "r")) == 0) {
      dsme_log(LOG_DEBUG, "%s: %s", ALARM_STATE_FILE, strerror(errno));
  } else {
      if (fscanf(f, "%ld, %ld, %ld", &active, &desktop, &actdead) != 3) {
          dsme_log(LOG_DEBUG, "Error reading file %s", ALARM_STATE_FILE);
      } else {
          alarm_state_file_up_to_date = true;
      }

      (void)fclose(f);
  }

  if (alarm_state_file_up_to_date) {
      dsme_log(LOG_DEBUG,
               "Alarm queue status restored: %ld, %ld, %ld",
               active,
               desktop,
               actdead);
  } else {
      dsme_log(LOG_WARNING, "Restoring alarm queue status failed");
  }
}


static int seconds(time_t from, time_t to)
{
  return (to <= 0 || to >= INT_MAX) ? 9999 : (to < from) ? 0 : (int)(to - from);
}

static int set_alarm_state(void* dummy)
{
  static bool alarm_set            = false;

  time_t      now                  = time(0);
  bool        alarm_previously_set = alarm_set;

  /* kill any previous alarm state transition timer */
  if (alarm_state_transition_timer) {
      dsme_destroy_timer(alarm_state_transition_timer);
      alarm_state_transition_timer = 0;
  }

  if (active ||
      (seconds(now, desktop) <= SNOOZE_TIMEOUT ||
       seconds(now, actdead) <= SNOOZE_TIMEOUT))
  {
      /* alarm is either active or soon-to-be-active */
      alarm_set = true;
  } else {
      /* set a timer for transition from not set to soon-to-be-active */
      int transition = seconds(now, desktop < actdead ? desktop : actdead)
                     - SNOOZE_TIMEOUT;
      alarm_state_transition_timer = dsme_create_timer(transition,
                                                       set_alarm_state,
                                                       0);
      dsme_log(LOG_DEBUG, "next snooze in %d s", transition);

      alarm_set = false;
  }

  if (alarm_set != alarm_previously_set) {
      /* inform state module about changed alarm state */
      DSM_MSGTYPE_SET_ALARM_STATE msg =
        DSME_MSG_INIT(DSM_MSGTYPE_SET_ALARM_STATE);

      msg.alarm_set = alarm_set;

      dsme_log(LOG_DEBUG,
               "broadcasting alarm state: %s",
               alarm_set ? "set" : "not set");
      broadcast_internally(&msg);
  }

  return 0; /* stop the interval */
}

static void alarmd_queue_status_ind(const DsmeDbusMessage* ind)
{
  time_t new_active  = dsme_dbus_message_get_int(ind);
  time_t new_desktop = dsme_dbus_message_get_int(ind);
  time_t new_actdead = dsme_dbus_message_get_int(ind);
#if 0 /* non-booting alarms are not considered */
  time_t new_noboot  = dsme_dbus_message_get_int(ind);
#endif

  if (!alarm_state_file_up_to_date ||
      new_active  != active        ||
      new_desktop != desktop       ||
      new_actdead != actdead)
  {
      active  = new_active;
      desktop = new_desktop;
      actdead = new_actdead;

      dsme_log(LOG_DEBUG, "got new alarms: %ld, %ld, %ld", active, desktop, actdead);

      /* save alarm queue status in the logger thread */
      alarm_state_file_up_to_date = false;
      dsme_log_wakeup();
  } else {
      dsme_log(LOG_DEBUG, "got old alarms: %ld, %ld, %ld", active, desktop, actdead);
  }

  set_alarm_state(0);
}

static const dsme_dbus_signal_binding_t signals[] = {
  { alarmd_queue_status_ind, "com.nokia.alarmd", "queue_status_ind" },
  { 0, 0 }
};

static bool bound = false;

DSME_HANDLER(DSM_MSGTYPE_DBUS_CONNECT, client, msg)
{
  dsme_log(LOG_DEBUG, "DBUS_CONNECT");
  dsme_dbus_bind_signals(&bound, signals);
}

DSME_HANDLER(DSM_MSGTYPE_DBUS_DISCONNECT, client, msg)
{
  dsme_log(LOG_DEBUG, "DBUS_DISCONNECT");
  dsme_dbus_unbind_signals(&bound, signals);
}

module_fn_info_t message_handlers[] = {
  DSME_HANDLER_BINDING(DSM_MSGTYPE_DBUS_CONNECT),
  DSME_HANDLER_BINDING(DSM_MSGTYPE_DBUS_DISCONNECT),
  { 0 }
};


void module_init(module_t* handle)
{
  /* Do not connect to D-Bus; it is probably not started yet.
   * Instead, wait for DSM_MSGTYPE_DBUS_CONNECT.
   */

  dsme_log(LOG_DEBUG, "libalarmtracker.so loaded");

  restore_alarm_queue_status();

  /* attach a callback for saving alarms from the logger thread */
  dsme_log_cb_attach(save_alarm_queue_status_cb);

  set_alarm_state(0);
}

void module_fini(void)
{
  dsme_dbus_unbind_signals(&bound, signals);

  dsme_log_cb_detach(save_alarm_queue_status_cb);

  dsme_log(LOG_DEBUG, "libalarmtracker.so unloaded");
}
