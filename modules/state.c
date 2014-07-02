/**
   @file state.c

   This file implements device state policy in DSME.
   <p>
   Copyright (C) 2004-2010 Nokia Corporation.

   @author Ismo Laitinen <ismo.laitinen@nokia.com>
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

/*
 * How to send runlevel change indicator:
 * dbus-send --system --type=signal /com/nokia/startup/signal com.nokia.startup.signal.runlevel_switch_done int32:0
 * where the int32 parameter is either 2 (user) or 5 (actdead).
 */

#define _POSIX_SOURCE

#include "dbusproxy.h"
#include "dsme_dbus.h"

#include "state-internal.h"
#include "runlevel.h"
#include "malf.h"
#include "dsme/timers.h"
#include "dsme/modules.h"
#include "dsme/logging.h"
#include "dsme/modulebase.h"
#include <dsme/state.h>

#include "dsme/dsme-rd-mode.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>

#ifdef DSME_VIBRA_FEEDBACK
#include "vibrafeedback.h"
#endif

#define PFIX "state: "

/**
 * Timer value for actdead shutdown timer. This is how long we wait before doing a
 * shutdown when the charger is disconnected in acting dead state
 */
#define CHARGER_DISCONNECT_TIMEOUT 15

/**
 * Timer value for shutdown timer. This is how long we wait for apps to close.
 */
#define SHUTDOWN_TIMER_TIMEOUT 2

/**
 * Timer value for acting dead timer. This is how long we wait before doing a
 * state change from user to acting dead.
 */
#define ACTDEAD_TIMER_MIN_TIMEOUT 2
#define ACTDEAD_TIMER_MAX_TIMEOUT 45

/**
 * Timer value for user timer. This is how long we wait before doing a
 * state change from acting dead to user.
 */
#define USER_TIMER_MIN_TIMEOUT 2
#define USER_TIMER_MAX_TIMEOUT 45

/* Seconds from overheating or empty battery to the start of shutdown timer */
#define DSME_THERMAL_SHUTDOWN_TIMER       8
#define DSME_BATTERY_EMPTY_SHUTDOWN_TIMER 8

/** 
 * Minimum battery level % that is needed before we allow
 * switch from ACTDEAD to USER
 * TODO: don't hard code this but support config value
 */
#define DSME_MINIMUM_BATTERY_TO_USER    3

#define BATTERY_LEVEL_PATH   "/run/state/namespaces/Battery/ChargePercentage"

typedef enum {
    CHARGER_STATE_UNKNOWN,
    CHARGER_CONNECTED,
    CHARGER_DISCONNECTED,
} charger_state_t;


/* these are the state bits on which dsme bases its state selection */
charger_state_t charger_state          = CHARGER_STATE_UNKNOWN;
static bool     alarm_set              = false;
static bool     device_overheated      = false;
static bool     emergency_call_ongoing = false;
static bool     mounted_to_pc          = false;
static bool     battery_empty          = false;
static bool     shutdown_requested     = false;
static bool     actdead_requested      = false;
static bool     reboot_requested       = false;
static bool     test                   = false;
static bool     actdead_switch_done    = false;
static bool     user_switch_done       = false;

/* the overall dsme state which was selected based on the above bits */
static dsme_state_t current_state = DSME_STATE_NOT_SET;

/* timers for delayed setting of state bits */
static dsme_timer_t overheat_timer           = 0;
static dsme_timer_t charger_disconnect_timer = 0;

/* timers for giving other programs a bit of time before shutting down */
static dsme_timer_t delayed_shutdown_timer   = 0;
static dsme_timer_t delayed_actdead_timer    = 0;
static dsme_timer_t delayed_user_timer       = 0;

#ifdef DSME_VIBRA_FEEDBACK
static const char low_battery_event_name[] = "low_battery_vibra_only";
#endif

static void change_state_if_necessary(void);
static void try_to_change_state(dsme_state_t new_state);
static void change_state(dsme_state_t new_state);
static void deny_state_change_request(dsme_state_t denied_state,
                                      const char*  reason);

static void start_delayed_shutdown_timer(unsigned seconds);
static int  delayed_shutdown_fn(void* unused);
#ifdef DSME_SUPPORT_DIRECT_USER_ACTDEAD
static bool start_delayed_actdead_timer(unsigned seconds);
static bool start_delayed_user_timer(unsigned seconds);
#endif
static int  delayed_actdead_fn(void* unused);
static int delayed_user_fn(void* unused);
static void stop_delayed_runlevel_timers(void);
static void change_runlevel(dsme_state_t state);

static void start_overheat_timer(void);
static int  delayed_overheat_fn(void* unused);

static void start_charger_disconnect_timer(void);
static int  delayed_charger_disconnect_fn(void* unused);
static void stop_charger_disconnect_timer(void);

static bool rd_mode_enabled(void);

static void runlevel_switch_ind(const DsmeDbusMessage* ind);
static int  get_battery_level(void);

static const struct {
    int         value;
    const char* name;
} states[] = {
#define DSME_STATE(STATE, VALUE) { VALUE, #STATE },
#include <dsme/state_states.h>
#undef  DSME_STATE
};

static const char* state_name(dsme_state_t state)
{
    int         index_sn;
    const char* name = "*** UNKNOWN STATE ***";;

    for (index_sn = 0; index_sn < sizeof states / sizeof states[0]; ++index_sn) {
        if (states[index_sn].value == state) {
            name = states[index_sn].name;
            break;
        }
    }

    return name;
}

static const dsme_state_t state_value(const char* name)
{
    int          index_sv;
    dsme_state_t state = DSME_STATE_NOT_SET;

    for (index_sv = 0; index_sv < sizeof states / sizeof states[0]; ++index_sv) {
        if (strcasecmp(states[index_sv].name, name) == 0) {
            state = states[index_sv].value;
            break;
        }
    }

    return state;
}

static dsme_runlevel_t state2runlevel(dsme_state_t state)
{
  dsme_runlevel_t runlevel;

  switch (state) {
      case DSME_STATE_SHUTDOWN: runlevel = DSME_RUNLEVEL_SHUTDOWN; break;
      case DSME_STATE_TEST:     runlevel = DSME_RUNLEVEL_TEST;     break;
      case DSME_STATE_USER:     runlevel = DSME_RUNLEVEL_USER;     break;
      case DSME_STATE_LOCAL:    runlevel = DSME_RUNLEVEL_LOCAL;
      case DSME_STATE_ACTDEAD:  runlevel = DSME_RUNLEVEL_ACTDEAD;  break;
      case DSME_STATE_REBOOT:   runlevel = DSME_RUNLEVEL_REBOOT;   break;

      case DSME_STATE_NOT_SET:  /* FALL THROUGH */
      case DSME_STATE_BOOT:     /* FALL THROUGH */
      default:                  runlevel = DSME_RUNLEVEL_SHUTDOWN; break;
  }

  return runlevel;
}


static dsme_state_t select_state(void)
{
  dsme_state_t state;

  if (emergency_call_ongoing) {

      /* don't touch anything if we have an emergency call going on */
      state = current_state;

  } else {

      if (test) {
          state = DSME_STATE_TEST;
      } else if (battery_empty) {
          dsme_log(LOG_CRIT, PFIX"Battery empty shutdown!");
          state = DSME_STATE_SHUTDOWN;
      } else if (device_overheated) {
          dsme_log(LOG_CRIT, PFIX"Thermal shutdown!");
          state = DSME_STATE_SHUTDOWN;
      } else if (actdead_requested) {
          /* favor actdead requests over shutdown & reboot */
          dsme_log(LOG_NOTICE, PFIX"Actdead by request");
          state = DSME_STATE_ACTDEAD;
      } else if (shutdown_requested || reboot_requested) {
          /* favor normal shutdown over reboot over actdead */
          if (shutdown_requested &&
              (charger_state == CHARGER_DISCONNECTED) &&
              !alarm_set)
          {
              dsme_log(LOG_NOTICE, PFIX"Normal shutdown");
              state = DSME_STATE_SHUTDOWN;
          } else if (reboot_requested) {
              dsme_log(LOG_NOTICE, PFIX"Reboot");
              state = DSME_STATE_REBOOT;
          } else{
              dsme_log(LOG_NOTICE,
                       PFIX"Actdead (charger: %s, alarm: %s)",
                       charger_state == CHARGER_CONNECTED ? "on"  : "off(?)",
                       alarm_set                          ? "set" : "not set");
              state = DSME_STATE_ACTDEAD;
          }
      } else {
          state = DSME_STATE_USER;
      }

  }
  return state;
}

static void change_state_if_necessary(void)
{
  dsme_state_t next_state = select_state();

  if (current_state != next_state) {
      try_to_change_state(next_state);
  }
}

static void try_to_change_state(dsme_state_t new_state)
{
  dsme_log(LOG_INFO,
           PFIX"state change request: %s -> %s",
           state_name(current_state),
           state_name(new_state));

  switch (new_state) {

    case DSME_STATE_SHUTDOWN: /* Runlevel 0 */ /* FALL THROUGH */
    case DSME_STATE_REBOOT:   /* Runlevel 6 */
      change_state(new_state);
      start_delayed_shutdown_timer(SHUTDOWN_TIMER_TIMEOUT);
      break;

    case DSME_STATE_USER:    /* Runlevel 5 */ /* FALL THROUGH */
    case DSME_STATE_ACTDEAD: /* Runlevel 4 */
      if (current_state == DSME_STATE_NOT_SET) {
          /* we have just booted up; simply change the state */
          change_state(new_state);
      } else if (current_state == DSME_STATE_ACTDEAD) {
          /* We are in actdead and user state is wanted
           * We don't allow that to happen if battery level is too low
           */
          if (get_battery_level() < DSME_MINIMUM_BATTERY_TO_USER ) {
              dsme_log(LOG_WARNING,
                 PFIX"Battery level %d%% too low for %s state",
                 get_battery_level(),
                 state_name(new_state));
#ifdef DSME_VIBRA_FEEDBACK
              /* Indicate by vibra that boot is not possible */
              dsme_play_vibra(low_battery_event_name);
#endif
              /* We need to return initial ACT_DEAD shutdown_reqest
               * as it got cleared when USER state transfer was requested
               */
              shutdown_requested = true;
              break;
          }
          /* Battery ok, lets do it */
          user_switch_done = false;
#ifndef DSME_SUPPORT_DIRECT_USER_ACTDEAD
          /* We don't support direct transfer from ACTDEAD to USER
           * but do it via reboot.
           */
          dsme_log(LOG_DEBUG, PFIX"USER state requested, we do it via REBOOT");
          change_state(DSME_STATE_REBOOT);
          start_delayed_shutdown_timer(SHUTDOWN_TIMER_TIMEOUT);
#else
          if (actdead_switch_done) {
              /* actdead init done; runlevel change from actdead to user state */
              if (start_delayed_user_timer(USER_TIMER_MIN_TIMEOUT)) {
                  change_state(new_state);
              } 
          } else {
              /* actdead init not done; wait longer to change from actdead to user state */
              if (start_delayed_user_timer(USER_TIMER_MAX_TIMEOUT)) {
                  change_state(new_state);
              } 
          }
#endif /* DSME_SUPPORT_DIRECT_USER_ACTDEAD */
      } else if (current_state == DSME_STATE_USER) {
          actdead_switch_done = false;
#ifndef DSME_SUPPORT_DIRECT_USER_ACTDEAD
          /* We don't support direct transfer from USER to ACTDEAD
           * but do it via shutdown. Usb cable will wakeup the device again
           * and then we will boot to ACTDEAD
           * Force SHUTDOWN
           */
          dsme_log(LOG_DEBUG, PFIX"ACTDEAD state requested, we do it via SHUTDOWN");
          change_state(DSME_STATE_SHUTDOWN);
          start_delayed_shutdown_timer(SHUTDOWN_TIMER_TIMEOUT);
#else
          if (user_switch_done) {
              /* user init done; runlevel change from user to actdead state */
              if (start_delayed_actdead_timer(ACTDEAD_TIMER_MIN_TIMEOUT)) {
                  change_state(new_state);
              } 
          } else {
              /* user init not done; wait longer to change from user to actdead state */
              if (start_delayed_actdead_timer(ACTDEAD_TIMER_MAX_TIMEOUT)) {
                  change_state(new_state);
              } 
          }
#endif /* DSME_SUPPORT_DIRECT_USER_ACTDEAD */
      }
      break;

    case DSME_STATE_TEST:  /* fall through */
    case DSME_STATE_LOCAL: /* NOTE: test server is running */
      if (current_state == DSME_STATE_NOT_SET) {
          change_state(new_state);
      }
      break;

    default:
      dsme_log(LOG_WARNING,
               PFIX"not possible to change to state %s (%d)",
               state_name(new_state),
               new_state);
      break;
  }

}

/**
 * This function sends a message to clients to notify about state change.
 * SAVE_DATA message is also sent when going down.
 * @param state State that is being activated
 */
static void change_state(dsme_state_t new_state)
{
  if (new_state == DSME_STATE_SHUTDOWN ||
      new_state == DSME_STATE_ACTDEAD  || 
      new_state == DSME_STATE_REBOOT)
  {
      DSM_MSGTYPE_SAVE_DATA_IND save_msg =
        DSME_MSG_INIT(DSM_MSGTYPE_SAVE_DATA_IND);

      dsme_log(LOG_DEBUG, PFIX"sending SAVE_DATA");
      broadcast(&save_msg);
  }

  DSM_MSGTYPE_STATE_CHANGE_IND ind_msg =
    DSME_MSG_INIT(DSM_MSGTYPE_STATE_CHANGE_IND);

  ind_msg.state = new_state;
  dsme_log(LOG_DEBUG, PFIX"STATE_CHANGE_IND sent (%s)", state_name(new_state));
  broadcast(&ind_msg);

  dsme_log(LOG_NOTICE, PFIX"new state: %s", state_name(new_state));
  current_state = new_state;
}


static bool is_state_change_request_acceptable(dsme_state_t requested_state)
{
    bool acceptable = true;

    // do not allow shutdown/reboot when in usb mass storage mode
    if ((requested_state == DSME_STATE_SHUTDOWN ||
         requested_state == DSME_STATE_REBOOT) &&
        mounted_to_pc)
    {
        acceptable = false;
        deny_state_change_request(requested_state, "usb");
    }

    return acceptable;
}


static void deny_state_change_request(dsme_state_t denied_state,
                                      const char*  reason)
{
  DSM_MSGTYPE_STATE_REQ_DENIED_IND ind =
      DSME_MSG_INIT(DSM_MSGTYPE_STATE_REQ_DENIED_IND);

  ind.state = denied_state;
  broadcast_with_extra(&ind, strlen(reason) + 1, reason);
  dsme_log(LOG_CRIT,
           PFIX"%s denied due to: %s",
           (denied_state == DSME_STATE_SHUTDOWN ? "shutdown" : "reboot"),
           reason);
}


static void start_delayed_shutdown_timer(unsigned seconds)
{
  if (!delayed_shutdown_timer) {
      stop_delayed_runlevel_timers();
      if (!(delayed_shutdown_timer = dsme_create_timer(seconds,
                                                       delayed_shutdown_fn,
                                                       NULL)))
      {
          dsme_log(LOG_CRIT, PFIX"Could not create a shutdown timer; exit!");
          dsme_exit(EXIT_FAILURE);
          return;
      }
      dsme_log(LOG_NOTICE, PFIX"Shutdown or reboot in %i seconds", seconds);
  }
}

static int delayed_shutdown_fn(void* unused)
{
  DSM_MSGTYPE_SHUTDOWN msg = DSME_MSG_INIT(DSM_MSGTYPE_SHUTDOWN);
  msg.runlevel = state2runlevel(current_state);
  broadcast_internally(&msg);

  return 0; /* stop the interval */
}

#ifdef DSME_SUPPORT_DIRECT_USER_ACTDEAD
static bool start_delayed_actdead_timer(unsigned seconds)
{
  bool success = false;
  if (!delayed_shutdown_timer && !delayed_actdead_timer && !delayed_user_timer) {
      if (!(delayed_actdead_timer = dsme_create_timer(seconds,
                                                      delayed_actdead_fn,
                                                      NULL)))
      {
          dsme_log(LOG_CRIT, PFIX"Could not create an actdead timer; exit!");
          dsme_exit(EXIT_FAILURE);
          return false;
      }
      success = true;
      dsme_log(LOG_NOTICE, PFIX"Actdead in %i seconds", seconds);
  }
  return success;
}
#endif /* DSME_SUPPORT_DIRECT_USER_ACTDEAD */

static int delayed_actdead_fn(void* unused)
{

  change_runlevel(DSME_STATE_ACTDEAD);

  delayed_actdead_timer = 0;

  return 0; /* stop the interval */
}

#ifdef DSME_SUPPORT_DIRECT_USER_ACTDEAD
static bool start_delayed_user_timer(unsigned seconds)
{
  bool success = false;
  if (!delayed_shutdown_timer && !delayed_actdead_timer && !delayed_user_timer) {
      if (!(delayed_user_timer = dsme_create_timer(seconds,
                                                   delayed_user_fn,
                                                   NULL)))
      {
          dsme_log(LOG_CRIT, PFIX"Could not create a user timer; exit!");
          dsme_exit(EXIT_FAILURE);
          return false;
      }
      success = true;
      dsme_log(LOG_NOTICE, PFIX"User in %i seconds", seconds);
  }
  return success;
}
#endif /* DSME_SUPPORT_DIRECT_USER_ACTDEAD */

static int delayed_user_fn(void* unused)
{
  change_runlevel(DSME_STATE_USER);

  delayed_user_timer = 0;

  return 0; /* stop the interval */
}

static void change_runlevel(dsme_state_t state)
{
  DSM_MSGTYPE_CHANGE_RUNLEVEL msg = DSME_MSG_INIT(DSM_MSGTYPE_CHANGE_RUNLEVEL);
  msg.runlevel = state2runlevel(state);
  broadcast_internally(&msg);
}

static void stop_delayed_runlevel_timers(void)
{
    if (delayed_shutdown_timer) {
        dsme_destroy_timer(delayed_shutdown_timer);
        delayed_shutdown_timer = 0;
        dsme_log(LOG_NOTICE, PFIX"Delayed shutdown timer stopped");
    }
    if (delayed_actdead_timer) {
        dsme_destroy_timer(delayed_actdead_timer);
        delayed_actdead_timer = 0;
        dsme_log(LOG_NOTICE, PFIX"Delayed actdead timer stopped");
    }
    if (delayed_user_timer) {
        dsme_destroy_timer(delayed_user_timer);
        delayed_user_timer = 0;
        dsme_log(LOG_NOTICE, PFIX"Delayed user timer stopped");
    }
}


static void start_overheat_timer(void)
{
  if (!overheat_timer) {
      if (!(overheat_timer = dsme_create_timer(DSME_THERMAL_SHUTDOWN_TIMER,
                                               delayed_overheat_fn,
                                               NULL)))
      {
          dsme_log(LOG_CRIT, PFIX"Could not create a timer; overheat immediately!");
          delayed_overheat_fn(0);
      } else {
          dsme_log(LOG_CRIT,
                   PFIX"Thermal shutdown in %d seconds",
                   DSME_THERMAL_SHUTDOWN_TIMER);
      }
  }
}

static int delayed_overheat_fn(void* unused)
{
  device_overheated = true;
  change_state_if_necessary();

  return 0; /* stop the interval */
}

static void start_charger_disconnect_timer(void)
{
  if (!charger_disconnect_timer) {
      if (!(charger_disconnect_timer = dsme_create_timer(
                                           CHARGER_DISCONNECT_TIMEOUT,
                                           delayed_charger_disconnect_fn,
                                           NULL)))
      {
          dsme_log(LOG_ERR,
                   PFIX"Could not create a timer; disconnect immediately!");
          delayed_charger_disconnect_fn(0);
      } else {
          dsme_log(LOG_DEBUG,
                   PFIX"Handle charger disconnect in %d seconds",
                   CHARGER_DISCONNECT_TIMEOUT);
      }
  }
}

static int delayed_charger_disconnect_fn(void* unused)
{

  charger_state = CHARGER_DISCONNECTED;
  change_state_if_necessary();

  return 0; /* stop the interval */
}

static void stop_charger_disconnect_timer(void)
{
  if (charger_disconnect_timer) {
      dsme_destroy_timer(charger_disconnect_timer);
      charger_disconnect_timer = 0;
      dsme_log(LOG_DEBUG, PFIX"Charger disconnect timer stopped");

      /* the last we heard, the charger had just been disconnected */
      charger_state = CHARGER_DISCONNECTED;
  }
}


DSME_HANDLER(DSM_MSGTYPE_SET_CHARGER_STATE, conn, msg)
{
  charger_state_t new_charger_state;

  dsme_log(LOG_DEBUG,
           PFIX"charger %s state received",
           msg->connected ? "connected" : "disconnected");

  new_charger_state = msg->connected ? CHARGER_CONNECTED : CHARGER_DISCONNECTED;

  stop_charger_disconnect_timer();

  if (current_state     == DSME_STATE_ACTDEAD    &&
      new_charger_state == CHARGER_DISCONNECTED  &&
      charger_state     != CHARGER_STATE_UNKNOWN)
  {
      /*
       * We are in acting dead, and the charger is disconnected.
       * Moreover, this is not the first time charger is disconnected;
       * shutdown after a while if charger is not connected again
       */
      start_charger_disconnect_timer();
  } else {
      charger_state = new_charger_state;
      change_state_if_necessary();
  }
}


DSME_HANDLER(DSM_MSGTYPE_SET_USB_STATE, conn, msg)
{
    if (mounted_to_pc != msg->mounted_to_pc) {
        mounted_to_pc  = msg->mounted_to_pc;

        dsme_log(LOG_INFO, PFIX"%smounted over USB", mounted_to_pc ? "" : "not ");
    }
}


// handlers for telinit requests
static void handle_telinit_NOT_SET(endpoint_t* conn)
{
    dsme_log(LOG_WARNING, PFIX"ignoring unknown telinit runlevel request");
}

static void handle_telinit_SHUTDOWN(endpoint_t* conn)
{
    if (is_state_change_request_acceptable(DSME_STATE_SHUTDOWN)) {
        shutdown_requested = true;
        actdead_requested  = false;
        change_state_if_necessary();
    }
}

static void handle_telinit_USER(endpoint_t* conn)
{
    shutdown_requested = false;
    actdead_requested  = false;
    change_state_if_necessary();
}

static void handle_telinit_ACTDEAD(endpoint_t* conn)
{
    if (is_state_change_request_acceptable(DSME_STATE_ACTDEAD)) {
        actdead_requested = true;
        change_state_if_necessary();
    }
}

static void handle_telinit_REBOOT(endpoint_t* conn)
{
    if (is_state_change_request_acceptable(DSME_STATE_REBOOT)) {
        reboot_requested  = true;
        actdead_requested = false;
        change_state_if_necessary();
    }
}

static void handle_telinit_TEST(endpoint_t* conn)
{
    dsme_log(LOG_WARNING, PFIX"telinit TEST unimplemented");
}

static void handle_telinit_MALF(endpoint_t* conn)
{
    dsme_log(LOG_WARNING, PFIX"telinit MALF unimplemented");
}

static void handle_telinit_BOOT(endpoint_t* conn)
{
    dsme_log(LOG_WARNING, PFIX"telinit BOOT unimplemented");
}

static void handle_telinit_LOCAL(endpoint_t* conn)
{
    dsme_log(LOG_WARNING, PFIX"telinit LOCAL unimplemented");
}

typedef void (telinit_handler_fn_t)(endpoint_t* conn);

static telinit_handler_fn_t* telinit_handler(dsme_state_t state)
{
    static const struct {
        dsme_state_t          state;
        telinit_handler_fn_t* handler;
    } handlers[] = {
#define DSME_STATE(STATE, VALUE) \
        { DSME_STATE_ ## STATE, handle_telinit_ ## STATE },
#include <dsme/state_states.h>
#undef  DSME_STATE
    };

    int index_th;
    telinit_handler_fn_t* handler = handle_telinit_NOT_SET;

    for (index_th = 0; index_th < sizeof states / sizeof states[0]; ++index_th) {
        if (handlers[index_th].state == state) {
            handler = handlers[index_th].handler;
            break;
        }
    }

    return handler;
}

DSME_HANDLER(DSM_MSGTYPE_TELINIT, conn, msg)
{
    const char* runlevel = DSMEMSG_EXTRA(msg);
    char*       sender   = endpoint_name(conn);

    dsme_log(LOG_NOTICE,
             PFIX"got telinit '%s' from %s",
             runlevel ? runlevel : "(null)",
             sender   ? sender   : "(unknown)");
    free(sender);

    if (runlevel) {
        telinit_handler(state_value(runlevel))(conn);
    }
}

/**
 * Shutdown requested.
 * We go to actdead state if alarm is set (or snoozed) or charger connected.
 */
DSME_HANDLER(DSM_MSGTYPE_SHUTDOWN_REQ, conn, msg)
{
  char* sender = endpoint_name(conn);
  dsme_log(LOG_NOTICE,
           PFIX"shutdown request received from %s",
           (sender ? sender : "(unknown)"));
  free(sender);

  handle_telinit_SHUTDOWN(conn);
}

DSME_HANDLER(DSM_MSGTYPE_REBOOT_REQ, conn, msg)
{
  char* sender = endpoint_name(conn);
  dsme_log(LOG_NOTICE,
           PFIX"reboot request received from %s",
           (sender ? sender : "(unknown)"));
  free(sender);

  handle_telinit_REBOOT(conn);
}

/**
 * Power up requested.
 * This means ACTDEAD -> USER transition.
 */
DSME_HANDLER(DSM_MSGTYPE_POWERUP_REQ, conn, msg)
{
  char* sender = endpoint_name(conn);
  dsme_log(LOG_NOTICE,
           PFIX"powerup request received from %s",
           (sender ? sender : "(unknown)"));
  free(sender);

  handle_telinit_USER(conn);
}


DSME_HANDLER(DSM_MSGTYPE_SET_ALARM_STATE, conn, msg)
{
  dsme_log(LOG_DEBUG,
           PFIX"alarm %s state received",
           msg->alarm_set ? "set or snoozed" : "not set");

  alarm_set = msg->alarm_set;

  change_state_if_necessary();
}


DSME_HANDLER(DSM_MSGTYPE_SET_THERMAL_STATUS, conn, msg)
{
  dsme_log(LOG_NOTICE,
           PFIX"%s state received",
           (msg->status == DSM_THERMAL_STATUS_OVERHEATED) ? "overheated" :
           (msg->status == DSM_THERMAL_STATUS_LOWTEMP) ? "low temp warning" :
           "normal temp");

  if (msg->status == DSM_THERMAL_STATUS_OVERHEATED) {
      start_overheat_timer();
  } else {
      /* there is no going back from being overheated */
  }
}


DSME_HANDLER(DSM_MSGTYPE_SET_EMERGENCY_CALL_STATE, conn, msg)
{
  dsme_log(LOG_NOTICE,
           PFIX"emergency call %s state received",
           msg->ongoing ? "on" : "off");

  emergency_call_ongoing = msg->ongoing;

  if (emergency_call_ongoing) {
      /* stop all timers that could lead to shutdown */
      stop_delayed_runlevel_timers();
  }

  change_state_if_necessary();
}

static int delayed_battery_empty_fn(void* unused)
{
    battery_empty = true;
    change_state_if_necessary();

    return 0; /* stop the interval */
}

DSME_HANDLER(DSM_MSGTYPE_SET_BATTERY_STATE, conn, battery)
{
  dsme_log(LOG_NOTICE,
           PFIX"battery %s state received",
           battery->empty ? "empty" : "not empty");

  if (battery->empty) {
      /* we have to shut down; first send the notification */
      DSM_MSGTYPE_BATTERY_EMPTY_IND battery_empty_ind =
          DSME_MSG_INIT(DSM_MSGTYPE_BATTERY_EMPTY_IND);

      broadcast(&battery_empty_ind);

      /* then set up a delayed shutdown */
      if (!dsme_create_timer(DSME_BATTERY_EMPTY_SHUTDOWN_TIMER,
                             delayed_battery_empty_fn,
                             NULL))
      {
          dsme_log(LOG_ERR,
                   PFIX"Cannot create timer; battery empty shutdown immediately!");
          delayed_battery_empty_fn(0);
      } else {
          dsme_log(LOG_CRIT,
                   PFIX"Battery empty shutdown in %d seconds",
                   DSME_BATTERY_EMPTY_SHUTDOWN_TIMER);
      }
  }
}


DSME_HANDLER(DSM_MSGTYPE_STATE_QUERY, client, msg)
{
  DSM_MSGTYPE_STATE_CHANGE_IND ind_msg =
    DSME_MSG_INIT(DSM_MSGTYPE_STATE_CHANGE_IND);

  dsme_log(LOG_DEBUG, PFIX"state_query, state: %s", state_name(current_state));

  ind_msg.state = current_state;
  endpoint_send(client, &ind_msg);
}


/**
 * Reads the RD mode state and returns true if enabled
 */
static bool rd_mode_enabled(void)
{
  bool          enabled;

  if (dsme_rd_mode_enabled()) {
      dsme_log(LOG_NOTICE, PFIX"R&D mode enabled");
      enabled = true;
  } else {
      enabled = false;
      dsme_log(LOG_DEBUG, PFIX"R&D mode disabled");
  }

  return enabled;
}

/*
 * catches the D-Bus signal com.nokia.startup.signal.runlevel_switch_done,
 * which is emitted whenever the runlevel init scripts have been completed.
 */
static void runlevel_switch_ind(const DsmeDbusMessage* ind)
{
    /* The runlevel for which init was completed */
    int runlevel_ind = dsme_dbus_message_get_int(ind);

    switch (runlevel_ind) {
        case DSME_RUNLEVEL_ACTDEAD: {
            /*  USER -> ACTDEAD runlevel change done */
            actdead_switch_done = true;
            dsme_log(LOG_DEBUG, PFIX"USER -> ACTDEAD runlevel change done");

            /* Do we have a pending ACTDEAD -> USER timer? */
            if (delayed_user_timer) {
                /* Destroy timer and immediately switch to USER because init is done */
                dsme_destroy_timer(delayed_user_timer);
                delayed_user_fn(0);
            }
            break;
        }
        case DSME_RUNLEVEL_USER: {
            /* ACTDEAD -> USER runlevel change done */
            user_switch_done = true;
            dsme_log(LOG_DEBUG, PFIX"ACTDEAD -> USER runlevel change done");

            /* Do we have a pending USER -> ACTDEAD timer? */
            if (delayed_actdead_timer) {
                /* Destroy timer and immediately switch to ACTDEAD because init is done */
                dsme_destroy_timer(delayed_actdead_timer);
                delayed_actdead_fn(0);
            }
            break;
        }
        default: {
            /*
             * Currently, we only get a runlevel switch signal for USER and ACTDEAD (NB#199301)
             */
            dsme_log(LOG_NOTICE, PFIX"Unhandled runlevel switch indicator signal. runlevel: %i", runlevel_ind);
            break;
        }
    }
}

static bool bound = false;

static const dsme_dbus_signal_binding_t signals[] = {
    { runlevel_switch_ind, "com.nokia.startup.signal", "runlevel_switch_done" },
    { 0, 0 }
};

DSME_HANDLER(DSM_MSGTYPE_DBUS_CONNECT, client, msg)
{
  dsme_log(LOG_DEBUG, PFIX"DBUS_CONNECT");
  dsme_dbus_bind_signals(&bound, signals);
#ifdef DSME_VIBRA_FEEDBACK
  dsme_ini_vibrafeedback();
#endif // DSME_VIBRA_FEEDBACK
}

DSME_HANDLER(DSM_MSGTYPE_DBUS_DISCONNECT, client, msg)
{
  dsme_log(LOG_DEBUG, PFIX"DBUS_DISCONNECT");
  dsme_dbus_unbind_signals(&bound, signals);
}

module_fn_info_t message_handlers[] = {
      DSME_HANDLER_BINDING(DSM_MSGTYPE_STATE_QUERY),
      DSME_HANDLER_BINDING(DSM_MSGTYPE_TELINIT),
      DSME_HANDLER_BINDING(DSM_MSGTYPE_SHUTDOWN_REQ),
      DSME_HANDLER_BINDING(DSM_MSGTYPE_POWERUP_REQ),
      DSME_HANDLER_BINDING(DSM_MSGTYPE_REBOOT_REQ),
      DSME_HANDLER_BINDING(DSM_MSGTYPE_SET_ALARM_STATE),
      DSME_HANDLER_BINDING(DSM_MSGTYPE_SET_USB_STATE),
      DSME_HANDLER_BINDING(DSM_MSGTYPE_SET_CHARGER_STATE),
      DSME_HANDLER_BINDING(DSM_MSGTYPE_SET_THERMAL_STATUS),
      DSME_HANDLER_BINDING(DSM_MSGTYPE_SET_EMERGENCY_CALL_STATE),
      DSME_HANDLER_BINDING(DSM_MSGTYPE_SET_BATTERY_STATE),
      DSME_HANDLER_BINDING(DSM_MSGTYPE_DBUS_CONNECT),
      DSME_HANDLER_BINDING(DSM_MSGTYPE_DBUS_DISCONNECT),
      {0}
};


static void parse_malf_info(char*  malf_info,
                            char** reason,
                            char** component,
                            char** details)
{
    char* p = malf_info;
    char* r = 0;
    char* c = 0;
    char* d = 0;
    char* save;

    if (p) {
        if ((r = strtok_r(p, " ", &save))) {
            if ((c = strtok_r(0, " ", &save))) {
                d = strtok_r(0, "", &save);
            }
        }
    }

    if (reason) {
        *reason = r;
    }
    if (component) {
        *component = c;
    }
    if (details) {
        *details = d;
    }
}

static void enter_malf(const char* reason,
                       const char* component,
                       const char* details)
{
  char* malf_details   = details ? strdup(details) : 0;
  DSM_MSGTYPE_ENTER_MALF malf = DSME_MSG_INIT(DSM_MSGTYPE_ENTER_MALF);
  malf.reason          = strcmp(reason, "HARDWARE") ? DSME_MALF_SOFTWARE
                                                    : DSME_MALF_HARDWARE;
  malf.component       = strdup(component);

  if (malf_details) {
      broadcast_internally_with_extra(&malf,
                                      strlen(malf_details) + 1,
                                      malf_details);
  } else {
      broadcast_internally(&malf);
  }
}

/*
 * If string 'string' begins with prefix 'prefix',
 * DSME_SKIP_PREFIX returns a pointer to the first character
 * in the string after the prefix. If the character is a space,
 * it is skipped.
 * If the string does not begin with the prefix, 0 is returned.
 */
#define DSME_SKIP_PREFIX(string, prefix) \
    (strncmp(string, prefix, sizeof(prefix) - 1) \
     ? 0 \
     : string + sizeof(prefix) - (*(string + sizeof(prefix) - 1) != ' '))

static void set_initial_state_bits(const char* bootstate)
{
  const char* p         = 0;
  bool        must_malf = false;

  if (strcmp(bootstate, "SHUTDOWN") == 0) {
      /*
       * DSME_STATE_SHUTDOWN:
       * charger must be considered disconnected;
       * otherwise we end up in actdead
       */
      // TODO: does getbootstate ever return "SHUTDOWN"?
      charger_state      = CHARGER_DISCONNECTED;
      shutdown_requested = true;

  } else if ((p = DSME_SKIP_PREFIX(bootstate, "USER"))) {
      // DSME_STATE_USER with possible malf information

  } else if ((p = DSME_SKIP_PREFIX(bootstate, "ACT_DEAD"))) {
      // DSME_STATE_ACTDEAD with possible malf information
      shutdown_requested = true;

  } else if (strcmp(bootstate, "BOOT") == 0) {
      // DSME_STATE_REBOOT
      // TODO: does getbootstate ever return "BOOT"?
      reboot_requested = true;

  } else if (strcmp(bootstate, "LOCAL") == 0 ||
             strcmp(bootstate, "TEST")  == 0 ||
             strcmp(bootstate, "FLASH") == 0)
  {
      // DSME_STATE_TEST
      test = true;
  } else if ((p = DSME_SKIP_PREFIX(bootstate, "MALF"))) {
      // DSME_STATE_USER with malf information
      must_malf = true;
      if (!*p) {
          // there was no malf information, so supply our own
          p = "SOFTWARE bootloader";
      }
  } else {
      // DSME_STATE_USER with malf information
      p = "SOFTWARE bootloader unknown bootreason to dsme";
  }

  if (p && *p) {
      // we got a bootstate followed by malf information

      // If allowed to malf, enter malf
      if (must_malf || !rd_mode_enabled()) {
          char* reason    = 0;
          char* component = 0;
          char* details   = 0;

          char* malf_info = strdup(p);
          parse_malf_info(malf_info, &reason, &component, &details);
          enter_malf(reason, component, details);
          free(malf_info);

      } else {
          dsme_log(LOG_NOTICE, PFIX"R&D mode enabled, not entering MALF '%s'", p);
      }
  }
}

static int  get_battery_level(void)
{
    FILE* f;
    bool ok = false;
    int batterylevel;


    f = fopen(BATTERY_LEVEL_PATH, "r");
    if (f) {
        if (fscanf(f, "%d", &batterylevel) == 1) {
            ok = true;
        }
        fclose(f);
    }
    if (!ok) {
        dsme_log(LOG_ERR, PFIX"FAILED to read %s", BATTERY_LEVEL_PATH);
        batterylevel = DSME_MINIMUM_BATTERY_TO_USER; /* return fake, don't block state change */
    }
    return batterylevel;
}

void module_init(module_t* handle)
{
  /* Do not connect to D-Bus; it is probably not started yet.
   * Instead, wait for DSM_MSGTYPE_DBUS_CONNECT.
   */

  dsme_log(LOG_DEBUG, "state.so started");

  const char* bootstate = getenv("BOOTSTATE");
  if (!bootstate) {
      bootstate = "USER";
      dsme_log(LOG_NOTICE,
               PFIX"BOOTSTATE: No such environment variable, using '%s'",
               bootstate);
  } else {
      dsme_log(LOG_INFO, PFIX"BOOTSTATE: '%s'", bootstate);
  }

  set_initial_state_bits(bootstate);
  change_state_if_necessary();

  dsme_log(LOG_DEBUG, PFIX"Startup state: %s", state_name(current_state));
}

void module_fini(void)
{
  dsme_dbus_unbind_signals(&bound, signals);
#ifdef DSME_VIBRA_FEEDBACK
  dsme_fini_vibrafeedback();
#endif  // DSME_VIBRA_FEEDBACK
  dsme_log(LOG_DEBUG, "state.so unloaded");
}
