/**
   @file testmod_emergencycalltracker.c

   A test driver for the emergencycalltracker module
   <p>
   Copyright (C) 2011 Nokia Corporation

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

/* INTRUSIONS */

#include "../dsme/modulebase.c"
#include "../modules/dsme_dbus.h"

/* INCLUDES */

#include "modules/dbusproxy.h"
#include "modules/state-internal.h"
#include "dsme/modulebase.h"
#include "dsme/modules.h"
#include "dsme/mainloop.h"

#include <dsme/protocol.h>
#include <dsme/dsmesock.h>
#include <dsme/messages.h>
#include <dsme/state.h>
#include <dsme/alarm_limit.h>

#include <iphbd/iphb_internal.h>

#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

#include <glib.h>
#include <glib/gstdio.h>

/* utils */
#include "utils_misc.h"

/* STUBS */
#include "stub_dbus.h"
#include "stub_dsme_dbus.h"

/* TEST DRIVER */
#include "testdriver.h"

static void initialize(void)
{
  if (!dsme_log_open(LOG_METHOD_STDOUT, 7, false, "    ", 0, 0, "")) {
      fatal("dsme_log_open() failed");
  }

  initialize_dbus_stub();
}

static void finalize(void)
{
  cleanup_dbus_stub();
}

static module_t* emergencycalltracker_module = 0;

static void load_emergencycalltracker(void)
{
  gchar* module_name_tmp = g_strconcat(dsme_module_path, "emergencycalltracker.so", NULL);
  emergencycalltracker_module = load_module_under_test(module_name_tmp);

  DSM_MSGTYPE_DBUS_CONNECT msg = TEST_MSG_INIT(DSM_MSGTYPE_DBUS_CONNECT);
  send_message(emergencycalltracker_module, &msg);
  g_free(module_name_tmp);
}

static void unload_emergencycalltracker(void)
{
  DSM_MSGTYPE_DBUS_DISCONNECT msg = TEST_MSG_INIT(DSM_MSGTYPE_DBUS_DISCONNECT);
  send_message(emergencycalltracker_module, &msg);
  unload_module_under_test(emergencycalltracker_module);
  assert(g_slist_length(dbus_signal_bindings) == 0);
}

static void send_call_state_ind(const char* call_state)
{
  DBusMessage* call_state_ind_msg = dbus_message_new_signal("/com/nokia/mce", "com.nokia.mce.signal", "sig_call_state_ind");

  dbus_message_append_args(call_state_ind_msg,
                           DBUS_TYPE_STRING, &call_state);

  dsme_dbus_stub_send_signal(call_state_ind_msg);
  free(call_state_ind_msg);
}

static void assert_emergency_call_status(bool ongoing)
{
  DSM_MSGTYPE_SET_EMERGENCY_CALL_STATE* emergency_call_msg;
  assert((emergency_call_msg = queued(DSM_MSGTYPE_SET_EMERGENCY_CALL_STATE)));
  assert(emergency_call_msg->ongoing == ongoing);
  free(emergency_call_msg);
}

/* TEST CASES */

static void test_emergency_call_ongoing(void)
{
  load_emergencycalltracker();

  send_call_state_ind("emergency");

  assert(!message_queue_is_empty());
  assert_emergency_call_status(true);
  assert(message_queue_is_empty());

  send_call_state_ind("none");
  assert(!message_queue_is_empty());
  assert_emergency_call_status(false);
  assert(message_queue_is_empty());

  unload_emergencycalltracker();
}

static void test_emergency_call_invalid_state(void)
{
  load_emergencycalltracker();

  send_call_state_ind("diibadaaba");

  // no DSM_MSGTYPE_SET_EMERGENCY_CALL_STATE message from an invalid call state
  assert(message_queue_is_empty());

  unload_emergencycalltracker();
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


  run(test_emergency_call_ongoing);
  run(test_emergency_call_invalid_state);

  finalize();

  fprintf(stderr, "\n[* * *   S U C C E S S   * * *]\n\n");

  return EXIT_SUCCESS;
}
