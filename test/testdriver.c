/**
   @file testdriver.c

   A simple test driver and test cases for DSME
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

#include "dsme/protocol.h"
#include "dsme/dsmesock.h"
#include "dsme/messages.h"
#include "dsme/modulebase.h"
#include "dsme/modules.h"
#include "dsme/mainloop.h"
#include "../modules/hwwd.h"

#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>


/* INTRUSIONS */

#include "../modulebase.c"

static bool message_queue_is_empty(void)
{
  int     count = 0;
  GSList* node;

  for (node = message_queue; node; node = node->next) {
      ++count;
  }

  if (count == 1) {
      fprintf(stderr, "[=> 1 more message queued]\n");
  } else if (count) {
      fprintf(stderr, "[=> %d more messages queued]\n", count);
  } else {
      fprintf(stderr, "[=> no more messages]\n");
  }

  if (count != 0) {
      for (node = message_queue; node; node = node->next) {
          fprintf(stderr, "[%x]\n", ((queued_msg_t*)(node->data))->data->type_);
      }
  }

  return count == 0;
}

#define queued(T) ((T*)queued_(DSME_MSG_ID_(T), #T))
static void* queued_(unsigned type, const char* name)
{
  dsmemsg_generic_t* msg = 0;
  GSList*            node;

  for (node = message_queue; node; node = node->next)
  {
      queued_msg_t* m = node->data;

      if (m->data->type_ == type) {
          msg = m->data;
          free(m);
          message_queue = g_slist_delete_link(message_queue, node);
          break;
      }
  }

  fprintf(stderr, msg ? "[=> %s was queued]\n" : "[=> %s was not queued\n", name);
  return msg;
}

#define TEST_MSG_INIT(T) DSME_MSG_INIT(T); fprintf(stderr, "\n[%s ", #T)

static void send_message(const module_t* module, const void* msg)
{
  endpoint_t endpoint = { module, 0 };
  fprintf(stderr, " SENT]\n");
  handle_message(&endpoint, module, msg);
}


/* UTILITY */

static void fatal(const char* format, ...)
{
  va_list ap;
  va_start(ap, format);
  fprintf(stderr, format, ap);
  fprintf(stderr, "\n");
  va_end(ap);
  exit(EXIT_FAILURE);
}


/* STUBS */

struct cal;

int cal_read_block(struct cal*    cal,
                   const char*    name,
                   void**         ptr,
                   unsigned long* len,
                   unsigned long  flags);

#define DEFAULT_RD_MODE "1"
static const char* rd_mode = DEFAULT_RD_MODE;

int cal_read_block(struct cal*    cal,
                   const char*    name,
                   void**         ptr,
                   unsigned long* len,
                   unsigned long  flags)
{
  int result = -1;

  if (strcmp(name, "r&d_mode") == 0) {
      if (rd_mode) {
          *ptr = malloc(2);
          strcpy(*ptr, rd_mode);
          *len = strlen(rd_mode);
          result = 0;
      }
  } else {
      fatal("cal_read_block(\"%s\")", name);
  }

  return result;
}

#include "dsme/timers.h"

typedef struct test_timer_t {
    unsigned               seconds;
    dsme_timer_callback_t* callback;
    void*                  data;
} test_timer_t;

static unsigned      timers = 0;
static test_timer_t* timer  = 0;

static void reset_timers(void)
{
  free(timer);
  timer  = 0;
  timers = 0;
}

static bool timer_exists(void)
{
  int count = 0;
  int i;

  for (i = 0; i < timers; ++i) {
      if (timer[i].seconds != 0) {
          ++count;
      }
  }

  if (count == 1) {
      fprintf(stderr, "[=> 1 timer exists]\n");
  } else if (count) {
      fprintf(stderr, "[=> %d timers exist]\n", count);
  } else {
      fprintf(stderr, "[=> no timers]\n");
  }

  return count;
}

static void trigger_timer(void)
{
  /* first find the earliest timer... */
  int earliest = -1;
  int           i;

  for (i = 0; i < timers; ++i) {
      if (timer[i].seconds != 0) {
          if (earliest < 0 || timer[i].seconds < timer[earliest].seconds) {
              earliest = i;
          }
      }
  }

  /* ...then call it */
  assert(earliest >= 0);
  fprintf(stderr, "\n[TRIGGER TIMER %d]\n", earliest + 1);
  if (!timer[earliest].callback(timer[earliest].data)) {
      timer[earliest].seconds = 0;
  }
}

static int dsme_create_timer_fails = 0;

dsme_timer_t dsme_create_timer(unsigned               seconds,
                               dsme_timer_callback_t* callback,
                               void*                  data)
{
  if (!dsme_create_timer_fails) {
      ++timers;
      timer = realloc(timer, timers * sizeof(*timer));
      timer[timers - 1].seconds  = seconds;
      timer[timers - 1].callback = callback;
      timer[timers - 1].data     = data;
      fprintf(stderr, "[=> timer %u created for %u s]\n", timers, seconds);
      return timers;
  } else {
      --dsme_create_timer_fails;
      return 0;
  }
}

void dsme_destroy_timer(dsme_timer_t t)
{
  fprintf(stderr, "[=> destroying timer %u]\n", t);
  assert(t > 0 && t <= timers);
  assert(timer[t - 1].seconds != 0);
  timer[t - 1].seconds  = 0;
  timer[t - 1].callback = 0;
  timer[t - 1].data     = 0;
}


/* TEST DRIVER */

static module_t* load_module_under_test(const char* path)
{
  module_t* module = 0;
  char*     canonical;

  fprintf(stderr, "\n[LOADING MODULE %s]\n", path);

  canonical = realpath(path, 0);
  if (!canonical) {
      perror(path);
      fatal("realpath() failed");
  } else {
      if (!(module = load_module(canonical, 0))) {
          fatal("load_module() failed");
      }
      free(canonical);
  }

  return module;
}

static void unload_module_under_test(module_t* module)
{
  fprintf(stderr, "\n[UNLOADING MODULE]\n");
  if (!unload_module(module)) {
      fatal("unload_module() failed");
  }
}

static void initialize(void)
{
  if (!dsme_log_open(LOG_METHOD_STDOUT, 7, false, "    ", 0, 0, "")) {
      fatal("dsme_log_open() failed");
  }
}

static void finalize(void)
{
}

typedef void (testcase)(void);

#define run(TC) run_(TC, #TC)
static void run_(testcase* test, const char* name)
{
  fprintf(stderr, "\n[ ******** STARTING TESTCASE '%s' ******** ]\n", name);
  reset_timers();
  test();
}


/* TEST CASE HELPERS */

#include "../modules/state.h"
#include "../modules/runlevel.h"
#include "../modules/lifeguard.h"

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

  setenv("BOOTSTATE", bootstate, true);
  module = load_module_under_test("../modules/libstate.so");
  unsetenv("BOOTSTATE");

  DSM_MSGTYPE_STATE_CHANGE_IND* ind;
  assert((ind = queued(DSM_MSGTYPE_STATE_CHANGE_IND)));
  assert(ind->state == expected_state);
  free(ind);

  if (expected_state == DSME_STATE_ACTDEAD) {
      DSM_MSGTYPE_SAVE_DATA_IND* ind2;
      assert(ind2 = queued(DSM_MSGTYPE_SAVE_DATA_IND));
      free(ind2);
  }

  assert(message_queue_is_empty());

  return module;
}

static void request_shutdown_expecting_reboot(module_t* state)
{
  DSM_MSGTYPE_SHUTDOWN_REQ msg = TEST_MSG_INIT(DSM_MSGTYPE_SHUTDOWN_REQ);
  send_message(state, &msg);

  DSM_MSGTYPE_STATE_CHANGE_IND* ind;
  assert((ind = queued(DSM_MSGTYPE_STATE_CHANGE_IND)));
  assert(ind->state == DSME_STATE_REBOOT);
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

  DSM_MSGTYPE_HWWD_KICK* ind3;
  assert((ind3 = queued(DSM_MSGTYPE_HWWD_KICK)));
  free(ind3);

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


/* TEST CASES */

static void testcase1(void)
{
  /* request shutdown right after starting in user state */

  module_t* state = load_state_module("USER", DSME_STATE_USER);
  assert(!timer_exists());

  request_shutdown_expecting_reboot(state);

  unload_module_under_test(state);
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

  request_shutdown_expecting_reboot(state);

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

  request_shutdown_expecting_reboot(state);

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

  DSM_MSGTYPE_HWWD_KICK* ind3;
  assert((ind3 = queued(DSM_MSGTYPE_HWWD_KICK)));
  free(ind3);

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

  DSM_MSGTYPE_STATE_CHANGE_IND* ind;
  assert((ind = queued(DSM_MSGTYPE_STATE_CHANGE_IND)));
  assert(ind->state == DSME_STATE_SHUTDOWN);
  free(ind);

  DSM_MSGTYPE_SAVE_DATA_IND* ind2;
  assert((ind2 = queued(DSM_MSGTYPE_SAVE_DATA_IND)));
  free(ind2);

  // expect shutdown
  trigger_timer();
  DSM_MSGTYPE_SHUTDOWN* msg2;
  assert((msg2 = queued(DSM_MSGTYPE_SHUTDOWN)));
  assert(msg2->runlevel == 0);
  free(msg2);
  DSM_MSGTYPE_HWWD_KICK* ind3;
  assert((ind3 = queued(DSM_MSGTYPE_HWWD_KICK)));
  free(ind3);
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

  DSM_MSGTYPE_HWWD_KICK* ind3;
  assert((ind3 = queued(DSM_MSGTYPE_HWWD_KICK)));
  free(ind3);

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
  assert(ind->state == DSME_STATE_REBOOT);
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

  DSM_MSGTYPE_SET_THERMAL_STATE msg =
      TEST_MSG_INIT(DSM_MSGTYPE_SET_THERMAL_STATE);
  msg.overheated = true;
  send_message(state, &msg);

  DSM_MSGTYPE_THERMAL_SHUTDOWN_IND* ind;
  assert((ind = queued(DSM_MSGTYPE_THERMAL_SHUTDOWN_IND)));
  free(ind);

  assert(message_queue_is_empty());
  assert(timer_exists());

  trigger_timer();

  DSM_MSGTYPE_STATE_CHANGE_IND* ind2;
  assert((ind2 = queued(DSM_MSGTYPE_STATE_CHANGE_IND)));
  assert(ind2->state == DSME_STATE_SHUTDOWN);
  free(ind2);

  DSM_MSGTYPE_SAVE_DATA_IND* ind3;
  assert((ind3 = queued(DSM_MSGTYPE_SAVE_DATA_IND)));
  free(ind3);

  DSM_MSGTYPE_HWWD_KICK* ind4;
  assert((ind4 = queued(DSM_MSGTYPE_HWWD_KICK)));
  free(ind4);

  assert(message_queue_is_empty());
  assert(timer_exists());

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

static void testcase17(void)
{
  /*
   * 1. overheat
   * 2. cool down before shutdown
   */
  module_t* state = load_state_module("USER", DSME_STATE_USER);
  assert(!timer_exists());

  // overheat
  DSM_MSGTYPE_SET_THERMAL_STATE msg =
      TEST_MSG_INIT(DSM_MSGTYPE_SET_THERMAL_STATE);
  msg.overheated = true;
  send_message(state, &msg);

  DSM_MSGTYPE_THERMAL_SHUTDOWN_IND* ind;
  assert((ind = queued(DSM_MSGTYPE_THERMAL_SHUTDOWN_IND)));
  free(ind);

  assert(message_queue_is_empty());
  assert(timer_exists());

  // cool down
  DSM_MSGTYPE_SET_THERMAL_STATE msg2 =
      TEST_MSG_INIT(DSM_MSGTYPE_SET_THERMAL_STATE);
  msg2.overheated = false;
  send_message(state, &msg2);

  assert(message_queue_is_empty());
  assert(!timer_exists());

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

  request_shutdown_expecting_reboot(state);

  unload_module_under_test(state);
}

static void testcase20(void)
{
  /* weird $BOOTSTATE cases */

  // do not specify $BOOTSTATE
  module_t* state = load_module_under_test("../modules/libstate.so");
  DSM_MSGTYPE_STATE_CHANGE_IND* ind;
  assert((ind = queued(DSM_MSGTYPE_STATE_CHANGE_IND)));
  assert(ind->state == DSME_STATE_MALF);
  free(ind);
  assert(message_queue_is_empty());
  assert(!timer_exists());
  unload_module_under_test(state);

  // specify a bad $BOOTSTATE
  state = load_state_module("DIIBADAABA", DSME_STATE_MALF);
  assert(!timer_exists());
  assert(message_queue_is_empty());
  unload_module_under_test(state);

  // specify SHUTDOWN
  setenv("BOOTSTATE", "SHUTDOWN", true);
  state = load_module_under_test("../modules/libstate.so");
  unsetenv("BOOTSTATE");
  expect_shutdown(state);
  unload_module_under_test(state);

  // specify SHUTDOWN
  setenv("BOOTSTATE", "SHUTDOWN", true);
  state = load_module_under_test("../modules/libstate.so");
  unsetenv("BOOTSTATE");
  expect_shutdown(state);
  unload_module_under_test(state);

  // specify BOOT
  setenv("BOOTSTATE", "BOOT", true);
  state = load_module_under_test("../modules/libstate.so");
  unsetenv("BOOTSTATE");
  expect_reboot(state);
  unload_module_under_test(state);
}

static void testcase21(void)
{
  /* non-rd_mode cases and cal problems */

  // non-rd_mode
  rd_mode = "";
  setenv("BOOTSTATE", "DIIBADAABA", true);
  module_t* state = load_module_under_test("../modules/libstate.so");
  unsetenv("BOOTSTATE");
  DSM_MSGTYPE_STATE_CHANGE_IND* ind;
  assert((ind = queued(DSM_MSGTYPE_STATE_CHANGE_IND)));
  assert(ind->state == DSME_STATE_MALF);
  free(ind);
  DSM_MSGTYPE_HWWD_KICK* ind3;
  assert((ind3 = queued(DSM_MSGTYPE_HWWD_KICK)));
  free(ind3);
  assert(message_queue_is_empty());
  assert(timer_exists());
  trigger_timer();
  DSM_MSGTYPE_SHUTDOWN* msg;
  assert((msg = queued(DSM_MSGTYPE_SHUTDOWN)));
  assert(msg->runlevel == 8);
  free(msg);
  assert(message_queue_is_empty());
  assert(!timer_exists());
  unload_module_under_test(state);

  // cal problem
  rd_mode = 0;
  setenv("BOOTSTATE", "DIIBADAABA", true);
  state = load_module_under_test("../modules/libstate.so");
  unsetenv("BOOTSTATE");
  assert((ind = queued(DSM_MSGTYPE_STATE_CHANGE_IND)));
  assert(ind->state == DSME_STATE_MALF);
  free(ind);
  assert((ind3 = queued(DSM_MSGTYPE_HWWD_KICK)));
  free(ind3);
  assert(message_queue_is_empty());
  assert(timer_exists());
  trigger_timer();
  assert((msg = queued(DSM_MSGTYPE_SHUTDOWN)));
  assert(msg->runlevel == 8);
  free(msg);
  assert(message_queue_is_empty());
  assert(!timer_exists());
  unload_module_under_test(state);
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

  DSM_MSGTYPE_SET_THERMAL_STATE msg =
      TEST_MSG_INIT(DSM_MSGTYPE_SET_THERMAL_STATE);
  msg.overheated = true;
  send_message(state, &msg);

  DSM_MSGTYPE_THERMAL_SHUTDOWN_IND* ind;
  assert((ind = queued(DSM_MSGTYPE_THERMAL_SHUTDOWN_IND)));
  free(ind);

  DSM_MSGTYPE_STATE_CHANGE_IND* ind2;
  assert((ind2 = queued(DSM_MSGTYPE_STATE_CHANGE_IND)));
  assert(ind2->state == DSME_STATE_SHUTDOWN);
  free(ind2);

  DSM_MSGTYPE_SAVE_DATA_IND* ind3;
  assert((ind3 = queued(DSM_MSGTYPE_SAVE_DATA_IND)));
  free(ind3);

  DSM_MSGTYPE_HWWD_KICK* ind4;
  assert((ind4 = queued(DSM_MSGTYPE_HWWD_KICK)));
  free(ind4);

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

int main(int argc, const char* argv[])
{
  initialize();

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
