/**
   @file testmod-state.c

   A simple test driver and test cases for DSME
   <p>
   Copyright (C) 2009-2011 Nokia Corporation

   @author Semi Malinen <semi.malinen@nokia.com>
   @author Jyrki Hämäläinen <ext-jyrki.hamalainen@nokia.com>

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

/* INTRUSIONS */

#include "../dsme/modulebase.c"
#include "../modules/dsme_dbus.h"
#include "../modules/malf.h"

/* INCLUDES */

#include "dsme/modulebase.h"
#include "dsme/modules.h"
#include "dsme/mainloop.h"

#include <dsme/protocol.h>
#include <dsme/dsmesock.h>
#include <dsme/messages.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

/* utils */
#include "utils_misc.h"

/* STUBS */
#include "stub_cal.h"
#include "stub_timers.h"
#include "stub_dsme_dbus.h"


/* TEST DRIVER */
#include "testdriver.h"

static void initialize(void)
{
  if (!dsme_log_open(LOG_METHOD_STDOUT, 7, false, "    ", 0, 0, "")) {
      fatal("dsme_log_open() failed");
  }
  setenv("DSME_RD_FLAGS_ENV", rd_mode, true);
}

static void finalize(void)
{
}

/* TEST CASE HELPERS */

#include <dsme/state.h>
#include "../modules/runlevel.h"

static void set_charger_state(module_t* state, bool connected)
{
  DSM_MSGTYPE_SET_CHARGER_STATE msg =
      TEST_MSG_INIT(DSM_MSGTYPE_SET_CHARGER_STATE);
  msg.connected = connected;
  send_message(state, &msg);
}

static void connect_charger(module_t* state)
{
  set_charger_state(state, true);
}

static void disconnect_charger(module_t* state)
{
  set_charger_state(state, false);
}

static module_t* load_state_module(const char*  bootstate,
                                   dsme_state_t expected_state)
{
  module_t* module;
  gchar* module_name_tmp = g_strconcat(dsme_module_path, "state.so", NULL);

  setenv("BOOTSTATE", bootstate, true);
  module = load_module_under_test(module_name_tmp);
  unsetenv("BOOTSTATE");
  g_free(module_name_tmp);

  DSM_MSGTYPE_STATE_CHANGE_IND* ind;
  assert((ind = queued(DSM_MSGTYPE_STATE_CHANGE_IND)));
  assert(ind->state == expected_state);
  free(ind);

  if (expected_state == DSME_STATE_ACTDEAD) {
      DSM_MSGTYPE_SAVE_DATA_IND* ind2;
      assert(ind2 = queued(DSM_MSGTYPE_SAVE_DATA_IND));
      free(ind2);
  }

  // TODO: this assert is not valid in case we MALF when loading the module
  // assert(message_queue_is_empty());

  return module;
}

static void request_shutdown_expecting_actdead(module_t* state)
{
  DSM_MSGTYPE_SHUTDOWN_REQ msg = TEST_MSG_INIT(DSM_MSGTYPE_SHUTDOWN_REQ);
  send_message(state, &msg);

  DSM_MSGTYPE_STATE_CHANGE_IND* ind;
  assert((ind = queued(DSM_MSGTYPE_STATE_CHANGE_IND)));
  assert(ind->state == DSME_STATE_ACTDEAD);
  free(ind);

  DSM_MSGTYPE_SAVE_DATA_IND* ind2;
  assert((ind2 = queued(DSM_MSGTYPE_SAVE_DATA_IND)));
  free(ind2);

  assert(message_queue_is_empty());
  assert(timer_exists());

  trigger_timer();

  DSM_MSGTYPE_CHANGE_RUNLEVEL* msg2;
  assert((msg2 = queued(DSM_MSGTYPE_CHANGE_RUNLEVEL)));
  assert(msg2->runlevel == 5);
  free(msg2);

  assert(!timer_exists());
  assert(message_queue_is_empty());
}

static void expect_shutdown_or_reboot(module_t*    module,
                                      dsme_state_t state,
                                      int          runlevel)
{
  DSM_MSGTYPE_STATE_CHANGE_IND* ind;
  assert((ind = queued(DSM_MSGTYPE_STATE_CHANGE_IND)));
  assert(ind->state == state);
  free(ind);

  DSM_MSGTYPE_SAVE_DATA_IND* ind2;
  assert((ind2 = queued(DSM_MSGTYPE_SAVE_DATA_IND)));
  free(ind2);

  assert(message_queue_is_empty());
  assert(timer_exists());

  trigger_timer();

  DSM_MSGTYPE_SHUTDOWN* msg3;
  assert((msg3 = queued(DSM_MSGTYPE_SHUTDOWN)));
  assert(msg3->runlevel == runlevel);
  free(msg3);

  assert(!timer_exists());
  assert(message_queue_is_empty());
}

static void expect_shutdown(module_t* state)
{
  expect_shutdown_or_reboot(state, DSME_STATE_SHUTDOWN, 0);
}

static void expect_reboot(module_t* state)
{
  expect_shutdown_or_reboot(state, DSME_STATE_REBOOT, 6);
}

static void request_shutdown_expecting_shutdown(module_t* state)
{
  DSM_MSGTYPE_SHUTDOWN_REQ msg = TEST_MSG_INIT(DSM_MSGTYPE_SHUTDOWN_REQ);
  send_message(state, &msg);

  expect_shutdown(state);
}



/* LIBSTATE TEST CASES */

static void testcase1(void)
{
  /* request shutdown right after starting in user state */
  module_t* state = load_state_module("USER", DSME_STATE_USER);
  assert(!timer_exists());

  request_shutdown_expecting_actdead(state);

  unload_module_under_test(state);

  assert(g_slist_length(dbus_signal_bindings) == 0);
}

static void testcase2(void)
{
  /*
   * 1. request shutdown when charger is known to be plugged in
   */
  // boot to user state
  module_t* state = load_state_module("USER", DSME_STATE_USER);
  assert(!timer_exists());

  // request shutdown when charger connected
  connect_charger(state);
  assert(message_queue_is_empty());
  assert(!timer_exists());

  request_shutdown_expecting_actdead(state);

  unload_module_under_test(state);
}

static void testcase2b(void)
{
  /*
   * continue after booting to actdead
   * 2. unplug a known to be plugged in charger
   * 3. wait for the shutdown to happen
   */
  module_t* state = load_state_module("ACT_DEAD", DSME_STATE_ACTDEAD);
  assert(!timer_exists());

  // unplug charger
  connect_charger(state);
  assert(!timer_exists());
  assert(message_queue_is_empty());
  disconnect_charger(state);
  assert(timer_exists());
  assert(message_queue_is_empty());

  // plug charger
  connect_charger(state);
  assert(!timer_exists());

  // unplug charger
  disconnect_charger(state);
  assert(message_queue_is_empty());
  assert(timer_exists());

  // wait for the shutdown
  trigger_timer();
  expect_shutdown(state);

  unload_module_under_test(state);
}

static void testcase3(void)
{
  /*
   * 1. request shutdown when charger is known to be plugged in
   * 2. unplug charger
   * 3. plug the charger back in
   * 4. unplug charger
   * 5. wait for the shutdown to happen
   */
  // boot to user state
  module_t* state = load_state_module("USER", DSME_STATE_USER);
  assert(!timer_exists());

  // request shutdown when charger connected
  connect_charger(state);
  assert(message_queue_is_empty());
  assert(!timer_exists());

  request_shutdown_expecting_actdead(state);

  unload_module_under_test(state);
}

static void testcase4(void)
{
  /* request shutdown when charger is known to be unplugged */

  // boot to user state
  module_t* state = load_state_module("USER", DSME_STATE_USER);
  assert(!timer_exists());

  disconnect_charger(state);

  request_shutdown_expecting_shutdown(state);

  unload_module_under_test(state);
}

static void testcase5(void)
{
  /*
   * 1. request shutdown when charger is known to be unplugged
   * 2. plug in charger before timer runs out
   * => expect shutdown
   */
  module_t* state = load_state_module("USER", DSME_STATE_USER);
  assert(!timer_exists());

  // unplug charger
  disconnect_charger(state);
  assert(message_queue_is_empty());
  assert(!timer_exists());

  // request shutdown
  DSM_MSGTYPE_SHUTDOWN_REQ msg = TEST_MSG_INIT(DSM_MSGTYPE_SHUTDOWN_REQ);
  send_message(state, &msg);

  DSM_MSGTYPE_STATE_CHANGE_IND* ind;
  assert((ind = queued(DSM_MSGTYPE_STATE_CHANGE_IND)));
  assert(ind->state == DSME_STATE_SHUTDOWN);
  free(ind);

  DSM_MSGTYPE_SAVE_DATA_IND* ind2;
  assert((ind2 = queued(DSM_MSGTYPE_SAVE_DATA_IND)));
  free(ind2);

  assert(message_queue_is_empty());
  assert(timer_exists());

  // plug in charger
  connect_charger(state);
  assert(message_queue_is_empty());

  // expect shutdown
  trigger_timer();
  DSM_MSGTYPE_SHUTDOWN* msg2;
  assert((msg2 = queued(DSM_MSGTYPE_SHUTDOWN)));
  assert(msg2->runlevel == 0);
  free(msg2);
  assert(!timer_exists());
  assert(message_queue_is_empty());

  unload_module_under_test(state);
}

static void testcase6(void)
{
  module_t* state = load_state_module("USER", DSME_STATE_USER);
  assert(!timer_exists());

  // plug and unplug charger
  connect_charger(state);
  assert(message_queue_is_empty());
  assert(!timer_exists());

  disconnect_charger(state);
  assert(message_queue_is_empty());
  assert(!timer_exists());

  unload_module_under_test(state);
}


static void testcase7(void)
{
  /* start in actdead */
  module_t* state = load_state_module("ACT_DEAD", DSME_STATE_ACTDEAD);
  assert(!timer_exists());

  assert(message_queue_is_empty());
  assert(!timer_exists());

  unload_module_under_test(state);
}

static void testcase8(void)
{
  /* start in actdead and unplug the charger */
  module_t* state = load_state_module("ACT_DEAD", DSME_STATE_ACTDEAD);
  assert(!timer_exists());

  // unplug charger
  disconnect_charger(state);
  expect_shutdown(state);

  unload_module_under_test(state);
}

static void testcase9(void)
{
  /* start in actdead and plug and uplug the charger */
  module_t* state = load_state_module("ACT_DEAD", DSME_STATE_ACTDEAD);
  assert(!timer_exists());

  // plug charger
  connect_charger(state);
  assert(message_queue_is_empty());
  assert(!timer_exists());

  // unplug charger
  disconnect_charger(state);
  assert(message_queue_is_empty());
  assert(timer_exists());

  // wait for the shutdown
  trigger_timer();
  expect_shutdown(state);

  unload_module_under_test(state);
}


static void testcase10(void)
{
  /* try to shut down when an emergency call is ongoing */
  module_t* state = load_state_module("USER", DSME_STATE_USER);
  assert(!timer_exists());

  // unplug charger
  disconnect_charger(state);
  assert(message_queue_is_empty());
  assert(!timer_exists());

  // set up an emergency call
  DSM_MSGTYPE_SET_EMERGENCY_CALL_STATE msg =
      TEST_MSG_INIT(DSM_MSGTYPE_SET_EMERGENCY_CALL_STATE);
  msg.ongoing = true;
  send_message(state, &msg);

  // request shutdown
  DSM_MSGTYPE_SHUTDOWN_REQ msg2 = TEST_MSG_INIT(DSM_MSGTYPE_SHUTDOWN_REQ);
  send_message(state, &msg2);
  assert(message_queue_is_empty());
  assert(!timer_exists());

  // stop the emergency call
  msg.ongoing = false;
  send_message(state, &msg);
  expect_shutdown(state);

  unload_module_under_test(state);
}

static void testcase11(void)
{
  /* reboot */
  module_t* state = load_state_module("USER", DSME_STATE_USER);
  assert(!timer_exists());

  // unplug charger
  disconnect_charger(state);
  assert(message_queue_is_empty());
  assert(!timer_exists());

  // request reboot
  DSM_MSGTYPE_REBOOT_REQ msg = TEST_MSG_INIT(DSM_MSGTYPE_REBOOT_REQ);
  send_message(state, &msg);
  expect_reboot(state);

  unload_module_under_test(state);
}

static void testcase12(void)
{
  /* shutdown on empty battery */
  module_t* state = load_state_module("USER", DSME_STATE_USER);
  assert(!timer_exists());

  // unplug charger
  disconnect_charger(state);

  // indicate an empty battery
  DSM_MSGTYPE_SET_BATTERY_STATE msg =
      TEST_MSG_INIT(DSM_MSGTYPE_SET_BATTERY_STATE);
  msg.empty = true;
  send_message(state, &msg);

  assert(timer_exists());
  trigger_timer();

  DSM_MSGTYPE_BATTERY_EMPTY_IND* ind;
  assert((ind = queued(DSM_MSGTYPE_BATTERY_EMPTY_IND)));
  free(ind);

  DSM_MSGTYPE_STATE_CHANGE_IND* ind2;
  assert((ind2 = queued(DSM_MSGTYPE_STATE_CHANGE_IND)));
  assert(ind2->state == DSME_STATE_SHUTDOWN);
  free(ind2);

  DSM_MSGTYPE_SAVE_DATA_IND* ind3;
  assert((ind3 = queued(DSM_MSGTYPE_SAVE_DATA_IND)));
  free(ind3);

  // expect shutdown
  trigger_timer();
  DSM_MSGTYPE_SHUTDOWN* msg2;
  assert((msg2 = queued(DSM_MSGTYPE_SHUTDOWN)));
  assert(msg2->runlevel == 0);
  free(msg2);
  assert(!timer_exists());
  assert(message_queue_is_empty());

  unload_module_under_test(state);
}

static void testcase13(void)
{
  /*
   * 1. request shutdown when charger is known to be unplugged
   * 2. start emergency call before timer runs out
   * 3. stop emergency call
   */
  module_t* state = load_state_module("USER", DSME_STATE_USER);
  assert(!timer_exists());

  // unplug charger
  disconnect_charger(state);
  assert(message_queue_is_empty());
  assert(!timer_exists());

  // request shutdown
  DSM_MSGTYPE_SHUTDOWN_REQ msg = TEST_MSG_INIT(DSM_MSGTYPE_SHUTDOWN_REQ);
  send_message(state, &msg);

  DSM_MSGTYPE_STATE_CHANGE_IND* ind;
  assert((ind = queued(DSM_MSGTYPE_STATE_CHANGE_IND)));
  assert(ind->state == DSME_STATE_SHUTDOWN);
  free(ind);

  DSM_MSGTYPE_SAVE_DATA_IND* ind2;
  assert((ind2 = queued(DSM_MSGTYPE_SAVE_DATA_IND)));
  free(ind2);

  assert(message_queue_is_empty());
  assert(timer_exists());

  // start emergency call
  DSM_MSGTYPE_SET_EMERGENCY_CALL_STATE msg2 =
      TEST_MSG_INIT(DSM_MSGTYPE_SET_EMERGENCY_CALL_STATE);
  msg2.ongoing = true;
  send_message(state, &msg2);
  assert(message_queue_is_empty());
  assert(!timer_exists());

  // stop emergency call
  msg2.ongoing = false;
  send_message(state, &msg2);

  // TODO: should we do a shutdown instead?
  assert(message_queue_is_empty());
  assert(!timer_exists());

  unload_module_under_test(state);
}

static void testcase14(void)
{
  /*
   * 1. request shutdown when charger is known to be plugged in
   * 2. start emergency call before timer runs out
   * 3. stop emergency call
   */
  module_t* state = load_state_module("USER", DSME_STATE_USER);
  assert(!timer_exists());

  // plug in charger
  connect_charger(state);
  assert(message_queue_is_empty());
  assert(!timer_exists());

  // request shutdown
  DSM_MSGTYPE_SHUTDOWN_REQ msg = TEST_MSG_INIT(DSM_MSGTYPE_SHUTDOWN_REQ);
  send_message(state, &msg);

  DSM_MSGTYPE_STATE_CHANGE_IND* ind;
  assert((ind = queued(DSM_MSGTYPE_STATE_CHANGE_IND)));
  assert(ind->state == DSME_STATE_ACTDEAD);
  free(ind);

  DSM_MSGTYPE_SAVE_DATA_IND* ind2;
  assert((ind2 = queued(DSM_MSGTYPE_SAVE_DATA_IND)));
  free(ind2);

  assert(message_queue_is_empty());
  assert(timer_exists());

  // start emergency call
  DSM_MSGTYPE_SET_EMERGENCY_CALL_STATE msg2 =
      TEST_MSG_INIT(DSM_MSGTYPE_SET_EMERGENCY_CALL_STATE);
  msg2.ongoing = true;
  send_message(state, &msg2);
  assert(message_queue_is_empty());
  assert(!timer_exists());

  // stop emergency call
  DSM_MSGTYPE_SET_EMERGENCY_CALL_STATE msg3 =
      TEST_MSG_INIT(DSM_MSGTYPE_SET_EMERGENCY_CALL_STATE);
  msg3.ongoing = false;
  send_message(state, &msg3);

  // TODO: should we go to actdead instead?
  assert(message_queue_is_empty());
  assert(!timer_exists());

  unload_module_under_test(state);
}

static void testcase15(void)
{
  /* emergency call */
  module_t* state = load_state_module("USER", DSME_STATE_USER);
  assert(!timer_exists());

  // start emergency call
  DSM_MSGTYPE_SET_EMERGENCY_CALL_STATE msg =
      TEST_MSG_INIT(DSM_MSGTYPE_SET_EMERGENCY_CALL_STATE);
  msg.ongoing = true;
  send_message(state, &msg);
  assert(message_queue_is_empty());
  assert(!timer_exists());

  // stop emergency call
  DSM_MSGTYPE_SET_EMERGENCY_CALL_STATE msg2 =
      TEST_MSG_INIT(DSM_MSGTYPE_SET_EMERGENCY_CALL_STATE);
  msg2.ongoing = false;
  send_message(state, &msg2);
  assert(message_queue_is_empty());
  assert(!timer_exists());

  unload_module_under_test(state);
}

static void testcase16(void)
{
  /* thermal shutdown due to overheating */
  module_t* state = load_state_module("USER", DSME_STATE_USER);
  assert(!timer_exists());

  DSM_MSGTYPE_SET_THERMAL_STATUS msg =
      TEST_MSG_INIT(DSM_MSGTYPE_SET_THERMAL_STATUS);
  msg.status = DSM_THERMAL_STATUS_OVERHEATED;
  send_message(state, &msg);

  assert(message_queue_is_empty());
  assert(timer_exists());

  trigger_timer();
  expect_shutdown(state);

  unload_module_under_test(state);
}

static void testcase17(void)
{
  /*
   * 1. overheat
   * 2. cool down before shutdown; we still have to shutdown
   */
  module_t* state = load_state_module("USER", DSME_STATE_USER);
  assert(!timer_exists());

  // overheat
  DSM_MSGTYPE_SET_THERMAL_STATUS msg =
      TEST_MSG_INIT(DSM_MSGTYPE_SET_THERMAL_STATUS);
  msg.status = DSM_THERMAL_STATUS_OVERHEATED;
  send_message(state, &msg);

  assert(message_queue_is_empty());
  assert(timer_exists());

  // cool down
  DSM_MSGTYPE_SET_THERMAL_STATUS msg2 =
      TEST_MSG_INIT(DSM_MSGTYPE_SET_THERMAL_STATUS);
  msg2.status = DSM_THERMAL_STATUS_NORMAL;
  send_message(state, &msg2);

  assert(message_queue_is_empty());
  assert(timer_exists());

  trigger_timer();
  expect_shutdown(state);
  unload_module_under_test(state);
}

static void testcase18(void)
{
  /*
   * 1. start in actdead
   * 2. request startup
   */
  module_t* state = load_state_module("ACT_DEAD", DSME_STATE_ACTDEAD);
  assert(!timer_exists());
  assert(message_queue_is_empty());

  // query state
  DSM_MSGTYPE_STATE_QUERY msg = TEST_MSG_INIT(DSM_MSGTYPE_STATE_QUERY);
  send_message(state, &msg);
  DSM_MSGTYPE_STATE_CHANGE_IND* ind;
  assert((ind = queued(DSM_MSGTYPE_STATE_CHANGE_IND)));
  assert(ind->state == DSME_STATE_ACTDEAD);
  free(ind);
  assert(message_queue_is_empty());
  assert(!timer_exists());

  // request startup
  DSM_MSGTYPE_POWERUP_REQ msg2 = TEST_MSG_INIT(DSM_MSGTYPE_POWERUP_REQ);
  send_message(state, &msg2);

  assert((ind = queued(DSM_MSGTYPE_STATE_CHANGE_IND)));
  assert(ind->state == DSME_STATE_USER);
  free(ind);

  assert(timer_exists());
  trigger_timer();

  DSM_MSGTYPE_CHANGE_RUNLEVEL* req;
  assert((req = queued(DSM_MSGTYPE_CHANGE_RUNLEVEL)));
  assert(req->runlevel == 2);
  assert(message_queue_is_empty());
  assert(!timer_exists());

  // query state
  send_message(state, &msg);
  assert((ind = queued(DSM_MSGTYPE_STATE_CHANGE_IND)));
  assert(ind->state == DSME_STATE_USER);
  free(ind);
  assert(message_queue_is_empty());
  assert(!timer_exists());
  unload_module_under_test(state);
}

static void testcase19(void)
{
  /* request shutdown when an alarm is about to happen */

  // boot to user state
  module_t* state = load_state_module("USER", DSME_STATE_USER);
  assert(!timer_exists());
  disconnect_charger(state);

  // set up an alarm
  DSM_MSGTYPE_SET_ALARM_STATE msg = TEST_MSG_INIT(DSM_MSGTYPE_SET_ALARM_STATE);
  msg.alarm_set = true;
  send_message(state, &msg);

  request_shutdown_expecting_actdead(state);

  unload_module_under_test(state);

}

static void testcase20(void)
{
  /* weird $BOOTSTATE cases */
  gchar* module_name_tmp = g_strconcat(dsme_module_path, "state.so", NULL);

  // do not specify $BOOTSTATE
  module_t* state = load_module_under_test(module_name_tmp);
  DSM_MSGTYPE_STATE_CHANGE_IND* ind;
  assert(ind = queued(DSM_MSGTYPE_STATE_CHANGE_IND));
  assert(ind->state == DSME_STATE_USER);
  free(ind);
  assert(message_queue_is_empty());
  assert(!timer_exists());
  unload_module_under_test(state);

  // specify a bad $BOOTSTATE
  state = load_state_module("DIIBADAABA", DSME_STATE_USER);

  DSM_MSGTYPE_ENTER_MALF* msg;
  assert(msg = queued(DSM_MSGTYPE_ENTER_MALF));
  free(msg);
  assert(!timer_exists());
  assert(message_queue_is_empty());
  unload_module_under_test(state);

  // specify SHUTDOWN
  setenv("BOOTSTATE", "SHUTDOWN", true);
  state = load_module_under_test(module_name_tmp);
  unsetenv("BOOTSTATE");
  expect_shutdown(state);
  unload_module_under_test(state);

  // specify SHUTDOWN
  setenv("BOOTSTATE", "SHUTDOWN", true);
  state = load_module_under_test(module_name_tmp);
  unsetenv("BOOTSTATE");
  expect_shutdown(state);
  unload_module_under_test(state);

  // specify BOOT
  setenv("BOOTSTATE", "BOOT", true);
  state = load_module_under_test(module_name_tmp);
  unsetenv("BOOTSTATE");
  expect_reboot(state);
  unload_module_under_test(state);

  g_free(module_name_tmp);
}

static void testcase21(void)
{
  /* non-rd_mode cases and cal problems */

  gchar* module_name_tmp = g_strconcat(dsme_module_path, "state.so", NULL);

  // non-rd_mode
  rd_mode = "";
  unsetenv("DSME_RD_FLAGS_ENV");
  setenv("BOOTSTATE", "DIIBADAABA", true);
  module_t* state = load_module_under_test(module_name_tmp);
  unsetenv("BOOTSTATE");
  DSM_MSGTYPE_STATE_CHANGE_IND* ind;
  assert(ind = queued(DSM_MSGTYPE_STATE_CHANGE_IND));
  assert(ind->state == DSME_STATE_USER);
  free(ind);
  assert(!message_queue_is_empty());
  DSM_MSGTYPE_ENTER_MALF* malfmsg;
  assert((malfmsg = queued(DSM_MSGTYPE_ENTER_MALF)));
  //TODO: Should the reason / component be checked?
  free(malfmsg);
  assert(message_queue_is_empty());
  assert(!timer_exists());
  unload_module_under_test(state);

  // cal problem
  rd_mode = 0;
  setenv("BOOTSTATE", "DIIBADAABA", true);
  state = load_module_under_test(module_name_tmp);
  unsetenv("BOOTSTATE");
  assert(ind = queued(DSM_MSGTYPE_STATE_CHANGE_IND));
  assert(ind->state == DSME_STATE_USER);
  free(ind);
  assert(!message_queue_is_empty());
  assert(malfmsg = queued(DSM_MSGTYPE_ENTER_MALF));
  //TODO: Should the reason / component be checked?
  free(malfmsg);
  assert(message_queue_is_empty());
  assert(!timer_exists());
  unload_module_under_test(state);

  g_free(module_name_tmp);
}

static void testcase22(void)
{
  /* TEST/LOCAL mode */

  // boot to TEST state
  module_t* state = load_state_module("TEST", DSME_STATE_TEST);
  assert(!timer_exists());
  unload_module_under_test(state);
}

static void testcase23(void)
{
  /* broken timers */

  // thermal shutdown when not able to create a timer
  dsme_create_timer_fails = 1;
  module_t* state = load_state_module("USER", DSME_STATE_USER);
  assert(!timer_exists());

  DSM_MSGTYPE_SET_THERMAL_STATUS msg =
      TEST_MSG_INIT(DSM_MSGTYPE_SET_THERMAL_STATUS);
  msg.status = DSM_THERMAL_STATUS_OVERHEATED;
  send_message(state, &msg);

  DSM_MSGTYPE_STATE_CHANGE_IND* ind2;
  assert((ind2 = queued(DSM_MSGTYPE_STATE_CHANGE_IND)));
  assert(ind2->state == DSME_STATE_SHUTDOWN);
  free(ind2);

  DSM_MSGTYPE_SAVE_DATA_IND* ind3;
  assert((ind3 = queued(DSM_MSGTYPE_SAVE_DATA_IND)));
  free(ind3);

  assert(message_queue_is_empty());
  assert(timer_exists());

  trigger_timer();
  DSM_MSGTYPE_SHUTDOWN* msg2;
  assert((msg2 = queued(DSM_MSGTYPE_SHUTDOWN)));
  assert(msg2->runlevel == 0);
  free(msg2);
  assert(!timer_exists());
  assert(message_queue_is_empty());

  unload_module_under_test(state);

  // start in actdead and plug and unplug the charger, but timer fails
  dsme_create_timer_fails = 1;
  state = load_state_module("ACT_DEAD", DSME_STATE_ACTDEAD);
  assert(!timer_exists());

  connect_charger(state);
  assert(message_queue_is_empty());
  assert(!timer_exists());

  disconnect_charger(state);
  expect_shutdown(state);

  assert(!timer_exists());
  assert(message_queue_is_empty());

  unload_module_under_test(state);
}


/* MAIN */

int main(int argc, char** argv)
{
  initialize();

  int optc;
  int opt_index;

  const char optline[] = "";
  struct option const options[] = {
      { "module-path", required_argument, 0, 'P' },
      { 0, 0, 0, 0 }
  };

  /* Parse the command-line options */
  while ((optc = getopt_long(argc, argv, optline,
			     options, &opt_index)) != -1) {
      switch (optc) {
      case 'P':
          dsme_module_path = strdup(optarg);
          break;

      default:
          fprintf(stderr, "\nInvalid parameters\n");
          fprintf(stderr, "\n[* * *   F A I L U R E   * * *]\n\n");
          return EXIT_SUCCESS;
      }
  }


  run(testcase1);
  run(testcase2);
  run(testcase2b);
  run(testcase3);
  run(testcase4);
  run(testcase5);
  run(testcase6);
  run(testcase7);
  run(testcase8);
  run(testcase9);
  run(testcase10);
  run(testcase11);
  run(testcase12);
  run(testcase13);
  run(testcase14);
  run(testcase15);
  run(testcase16);
  run(testcase17);
  run(testcase18);
  run(testcase19);
  run(testcase20);
  run(testcase21);
  run(testcase22);
  run(testcase23);

  finalize();

  fprintf(stderr, "\n[* * *   S U C C E S S   * * *]\n\n");

  return EXIT_SUCCESS;
}

