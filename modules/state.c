/**
   @file state.c

   This file implements device state policy in DSME.
   <p>
   Copyright (C) 2004-2010 Nokia Corporation.

   @author Ismo Laitinen <ismo.laitinen@nokia.com>
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

#define _POSIX_SOURCE

#include "state-internal.h"
#include "runlevel.h"
#include "dsme/timers.h"
#include "dsme/modules.h"
#include "dsme/logging.h"
#include <dsme/state.h>

#include "dsme-rd-mode.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>


/**
 * Timer value for actdead shutdown timer. This is how long we wait before doing a
 * shutdown when the charger is disconnected in acting dead state
 */
#define CHARGER_DISCONNECT_TIMEOUT 3

/**
 * Timer value for shutdown timer. This is how long we wait for apps to close.
 */
#define SHUTDOWN_TIMER_TIMEOUT 2

/* In non-R&D mode we shutdown after this many seconds in MALF */
#define MALF_SHUTDOWN_TIMER 120

/* Seconds from overheating or empty battery to the start of shutdown timer */
#define DSME_THERMAL_SHUTDOWN_TIMER       8
#define DSME_BATTERY_EMPTY_SHUTDOWN_TIMER 8


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
static bool     malf                   = false;

/* the overall dsme state which was selected based on the above bits */
static dsme_state_t current_state = DSME_STATE_NOT_SET;

/* timers for delayed setting of state bits */
static dsme_timer_t overheat_timer           = 0;
static dsme_timer_t charger_disconnect_timer = 0;

/* timers for giving other programs a bit of time before shutting down */
static dsme_timer_t delayed_shutdown_timer   = 0;
static dsme_timer_t delayed_actdead_timer    = 0;


static void change_state_if_necessary(void);
static void try_to_change_state(dsme_state_t new_state);
static void change_state(dsme_state_t new_state);
static void deny_state_change_request(dsme_state_t denied_state,
                                      const char*  reason);

static void start_delayed_shutdown_timer(unsigned seconds);
static int  delayed_shutdown_fn(void* unused);
static void start_delayed_actdead_timer(unsigned seconds);
static int  delayed_actdead_fn(void* unused);
static void stop_delayed_runlevel_timers(void);
static void change_runlevel(dsme_state_t state);
static void kick_wds(void);

static void start_overheat_timer(void);
static int  delayed_overheat_fn(void* unused);

static void start_charger_disconnect_timer(void);
static int  delayed_charger_disconnect_fn(void* unused);
static void stop_charger_disconnect_timer(void);

static bool rd_mode_enabled(void);


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
    int         index;
    const char* name = "*** UNKNOWN STATE ***";;

    for (index = 0; index < sizeof states / sizeof states[0]; ++index) {
        if (states[index].value == state) {
            name = states[index].name;
            break;
        }
    }

    return name;
}

static const dsme_state_t state_value(const char* name)
{
    int          index;
    dsme_state_t state = DSME_STATE_NOT_SET;

    for (index = 0; index < sizeof states / sizeof states[0]; ++index) {
        if (strcasecmp(states[index].name, name) == 0) {
            state = states[index].value;
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
      case DSME_STATE_MALF:     /* FALL THROUGH */
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

      if (malf) {
          state = DSME_STATE_MALF;
      } else if (test) {
          state = DSME_STATE_TEST;
      } else if (battery_empty) {
          dsme_log(LOG_CRIT, "Battery empty shutdown!");
          state = DSME_STATE_SHUTDOWN;
      } else if (device_overheated) {
          dsme_log(LOG_CRIT, "Thermal shutdown!");
          state = DSME_STATE_SHUTDOWN;
      } else if (actdead_requested) {
          /* favor actdead requests over shutdown & reboot */
          dsme_log(LOG_NOTICE, "Actdead by request");
          state = DSME_STATE_ACTDEAD;
      } else if (shutdown_requested || reboot_requested) {
          /* favor normal shutdown over reboot over actdead */
          if (shutdown_requested &&
              charger_state == CHARGER_DISCONNECTED &&
              !alarm_set)
          {
              dsme_log(LOG_NOTICE, "Normal shutdown");
              state = DSME_STATE_SHUTDOWN;
          } else if (reboot_requested) {
              dsme_log(LOG_NOTICE, "Reboot");
              state = DSME_STATE_REBOOT;
          } else{
              dsme_log(LOG_NOTICE,
                       "Actdead (charger: %s, alarm: %s)",
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
           "state change request: %s -> %s",
           state_name(current_state),
           state_name(new_state));

  switch (new_state) {

    case DSME_STATE_SHUTDOWN: /* Runlevel 0 */ /* FALL THROUGH */
    case DSME_STATE_REBOOT:   /* Runlevel 6 */
      change_state(new_state);
      start_delayed_shutdown_timer(SHUTDOWN_TIMER_TIMEOUT);
      break;

    case DSME_STATE_USER:    /* Runlevel 2 */ /* FALL THROUGH */
    case DSME_STATE_ACTDEAD: /* Runlevel 5 */
      if (current_state == DSME_STATE_NOT_SET) {
          /* we have just booted up; simply change the state */
          change_state(new_state);
      } else if (current_state == DSME_STATE_ACTDEAD) {
          /* immediate runlevel change from ACTDEAD to USER */
          change_state(new_state);
          change_runlevel(new_state);
      } else if (current_state == DSME_STATE_USER) {
          /* make a delayed runlevel change from user to actdead state */
          change_state(new_state);
          start_delayed_actdead_timer(SHUTDOWN_TIMER_TIMEOUT);
      }
      break;

    case DSME_STATE_TEST:  /* fall through */
    case DSME_STATE_LOCAL: /* NOTE: test server is running */
      if (current_state == DSME_STATE_NOT_SET) {
          change_state(new_state);
      }
      break;

    case DSME_STATE_MALF: /* Runlevel 8 */
      change_state(DSME_STATE_MALF);

      /* If R&D mode is not enabled, shutdown after the timer */
      if (!rd_mode_enabled()) {
          start_delayed_shutdown_timer(MALF_SHUTDOWN_TIMER);
      } else {
          dsme_log(LOG_NOTICE, "R&D mode enabled, not shutting down");
      }
      break;

    default:
      dsme_log(LOG_WARNING,
               "not possible to change to state %s (%d)",
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

      dsme_log(LOG_DEBUG, "sending SAVE_DATA");
      broadcast(&save_msg);
  }

  DSM_MSGTYPE_STATE_CHANGE_IND ind_msg =
    DSME_MSG_INIT(DSM_MSGTYPE_STATE_CHANGE_IND);

  ind_msg.state = new_state;
  dsme_log(LOG_DEBUG, "STATE_CHANGE_IND sent (%s)", state_name(new_state));
  broadcast(&ind_msg);

  dsme_log(LOG_NOTICE, "new state: %s", state_name(new_state));
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
  dsme_log(LOG_WARNING,
           "%s denied due to: %s",
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
          dsme_log(LOG_CRIT, "Could not create a shutdown timer; exit!");
          exit(EXIT_FAILURE);
      }
      dsme_log(LOG_NOTICE, "Shutdown or reboot in %i seconds", seconds);
  }
}

static int delayed_shutdown_fn(void* unused)
{
  /* first kick WD's for the last time */
  kick_wds();

  /* then do the shutdown */
  DSM_MSGTYPE_SHUTDOWN msg = DSME_MSG_INIT(DSM_MSGTYPE_SHUTDOWN);
  msg.runlevel = state2runlevel(current_state);
  broadcast_internally(&msg);

  return 0; /* stop the interval */
}

static void start_delayed_actdead_timer(unsigned seconds)
{
  if (!delayed_shutdown_timer && !delayed_actdead_timer) {
      if (!(delayed_actdead_timer = dsme_create_timer(seconds,
                                                      delayed_actdead_fn,
                                                      NULL)))
      {
          dsme_log(LOG_CRIT, "Could not create an actdead timer; exit!");
          exit(EXIT_FAILURE);
      }
      dsme_log(LOG_NOTICE, "Actdead in %i seconds", seconds);
  }
}

static int delayed_actdead_fn(void* unused)
{
  change_runlevel(DSME_STATE_ACTDEAD);

  delayed_actdead_timer = 0;

  return 0; /* stop the interval */
}

static void change_runlevel(dsme_state_t state)
{
  /* first kick WD's for the last time */
  kick_wds();

  /* then change the runlevel */
  DSM_MSGTYPE_CHANGE_RUNLEVEL msg = DSME_MSG_INIT(DSM_MSGTYPE_CHANGE_RUNLEVEL);
  msg.runlevel = state2runlevel(state);
  broadcast_internally(&msg);
}

// TODO: this could be removed since dsme will automatically kick wd's
//       when going down
static void kick_wds(void)
{
    kill(getppid(), SIGHUP);
}

static void stop_delayed_runlevel_timers(void)
{
    if (delayed_shutdown_timer) {
        dsme_destroy_timer(delayed_shutdown_timer);
        delayed_shutdown_timer = 0;
        dsme_log(LOG_NOTICE, "Delayed shutdown timer stopped");
    }
    if (delayed_actdead_timer) {
        dsme_destroy_timer(delayed_actdead_timer);
        delayed_actdead_timer = 0;
        dsme_log(LOG_NOTICE, "Delayed actdead timer stopped");
    }
}


static void start_overheat_timer(void)
{
  if (!overheat_timer) {
      if (!(overheat_timer = dsme_create_timer(DSME_THERMAL_SHUTDOWN_TIMER,
                                               delayed_overheat_fn,
                                               NULL)))
      {
          dsme_log(LOG_CRIT, "Could not create a timer; overheat immediately!");
          delayed_overheat_fn(0);
      } else {
          dsme_log(LOG_CRIT,
                   "Thermal shutdown in %d seconds",
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
                   "Could not create a timer; disconnect immediately!");
          delayed_charger_disconnect_fn(0);
      } else {
          dsme_log(LOG_DEBUG,
                   "Handle charger disconnect in %d seconds",
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
      dsme_log(LOG_DEBUG, "Charger disconnect timer stopped");

      /* the last we heard, the charger had just been disconnected */
      charger_state = CHARGER_DISCONNECTED;
  }
}


DSME_HANDLER(DSM_MSGTYPE_SET_CHARGER_STATE, conn, msg)
{
  charger_state_t new_charger_state;

  dsme_log(LOG_DEBUG,
           "charger %s state received",
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
    mounted_to_pc = msg->mounted_to_pc;

    dsme_log(LOG_INFO, "%smounted over USB", mounted_to_pc ? "" : "not ");
}


// handlers for telinit requests
static void handle_telinit_NOT_SET(endpoint_t* conn)
{
    dsme_log(LOG_WARNING, "ignoring unknown telinit runlevel request");
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
    dsme_log(LOG_WARNING, "telinit TEST unimplemented");
}

static void handle_telinit_MALF(endpoint_t* conn)
{
    dsme_log(LOG_WARNING, "telinit MALF unimplemented");
}

static void handle_telinit_BOOT(endpoint_t* conn)
{
    dsme_log(LOG_WARNING, "telinit BOOT unimplemented");
}

static void handle_telinit_LOCAL(endpoint_t* conn)
{
    dsme_log(LOG_WARNING, "telinit LOCAL unimplemented");
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

    int index;
    telinit_handler_fn_t* handler = handle_telinit_NOT_SET;

    for (index = 0; index < sizeof states / sizeof states[0]; ++index) {
        if (handlers[index].state == state) {
            handler = handlers[index].handler;
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
             "got telinit '%s' from %s",
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
           "shutdown request received from %s",
           (sender ? sender : "(unknown)"));
  free(sender);

  handle_telinit_SHUTDOWN(conn);
}

DSME_HANDLER(DSM_MSGTYPE_REBOOT_REQ, conn, msg)
{
  char* sender = endpoint_name(conn);
  dsme_log(LOG_NOTICE,
           "reboot request received from %s",
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
           "powerup request received from %s",
           (sender ? sender : "(unknown)"));
  free(sender);

  handle_telinit_USER(conn);
}


DSME_HANDLER(DSM_MSGTYPE_SET_ALARM_STATE, conn, msg)
{
  dsme_log(LOG_DEBUG,
           "alarm %s state received",
           msg->alarm_set ? "set or snoozed" : "not set");

  alarm_set = msg->alarm_set;

  change_state_if_necessary();
}


DSME_HANDLER(DSM_MSGTYPE_SET_THERMAL_STATE, conn, msg)
{
  dsme_log(LOG_NOTICE,
           "%s state received",
           msg->overheated ? "overheated" : "not overheated");

  if (msg->overheated) {
      start_overheat_timer();
  } else {
      /* there is no going back from being overheated */
  }
}


DSME_HANDLER(DSM_MSGTYPE_SET_EMERGENCY_CALL_STATE, conn, msg)
{
  dsme_log(LOG_NOTICE,
           "emergency call %s state received",
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
           "battery %s state received",
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
                   "Cannot create timer; battery empty shutdown immediately!");
          delayed_battery_empty_fn(0);
      } else {
          dsme_log(LOG_CRIT,
                   "Battery empty shutdown in %d seconds",
                   DSME_BATTERY_EMPTY_SHUTDOWN_TIMER);
      }
  }
}


DSME_HANDLER(DSM_MSGTYPE_STATE_QUERY, client, msg)
{
  DSM_MSGTYPE_STATE_CHANGE_IND ind_msg =
    DSME_MSG_INIT(DSM_MSGTYPE_STATE_CHANGE_IND);

  dsme_log(LOG_DEBUG, "state_query, state: %s", state_name(current_state));

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
      dsme_log(LOG_NOTICE, "R&D mode enabled");
      enabled = true;
  } else {
      enabled = false;
      dsme_log(LOG_DEBUG, "R&D mode disabled");
  }

  return enabled;
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
      DSME_HANDLER_BINDING(DSM_MSGTYPE_SET_THERMAL_STATE),
      DSME_HANDLER_BINDING(DSM_MSGTYPE_SET_EMERGENCY_CALL_STATE),
      DSME_HANDLER_BINDING(DSM_MSGTYPE_SET_BATTERY_STATE),
      {0}
};


static void set_initial_state_bits(const char* bootstate)
{
  if (strcmp(bootstate, "SHUTDOWN") == 0) {
      /*
       * DSME_STATE_SHUTDOWN:
       * charger must be considered disconnected;
       * otherwise we end up in actdead
       */
      charger_state      = CHARGER_DISCONNECTED;
      shutdown_requested = true;

  } else if (strcmp(bootstate, "USER") == 0) {
      /* DSME_STATE_USER: NOP */

  } else if (strcmp(bootstate, "ACT_DEAD") == 0) {
      /* DSME_STATE_ACTDEAD */
      shutdown_requested = true;

  } else if (strcmp(bootstate, "BOOT") == 0) {
      /* DSME_STATE_REBOOT */
      /* TODO: does getbootstate ever return "BOOT"? */
      reboot_requested = true;

  } else if (strcmp(bootstate, "LOCAL") == 0 ||
             strcmp(bootstate, "TEST")  == 0)
  {
      /* DSME_STATE_TEST */
      test = true;

  } else {
      /* DSME_STATE_MALF */
      malf = true;
  }
}

void module_init(module_t* handle)
{
  dsme_log(LOG_DEBUG, "libstate.so started");

  const char* bootstate = getenv("BOOTSTATE");
  if (!bootstate) {
      bootstate = "MALF";
      dsme_log(LOG_CRIT,
               "BOOTSTATE: No such environment variable, using '%s'",
               bootstate);
  } else {
      dsme_log(LOG_INFO, "BOOTSTATE: '%s'", bootstate);
  }

  set_initial_state_bits(bootstate);
  change_state_if_necessary();

  dsme_log(LOG_NOTICE, "Startup state: %s", state_name(current_state));
}

void module_fini(void)
{
  dsme_log(LOG_DEBUG, "libstate.so unloaded");
}
