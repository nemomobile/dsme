/**
   @file testmod_alarmtracker.c

   A simple test driver and test cases for DSME
   <p>
   Copyright (C) 2011 Nokia Corporation

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

#include "modules/dbusproxy.h"
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
#include "stub_timers.h"
#include "stub_dbus.h"
#include "stub_dsme_dbus.h"


/* TIME_STUB */
#include <time.h>
#include <dlfcn.h>

static time_t faketime = 0;
static unsigned char use_realtime = 0;

time_t time(time_t *tloc)
{
  time_t rval;

  if (use_realtime) {
      time_t (*realtime)(time_t *tloc) = dlsym(RTLD_NEXT, "time");
      if (dlerror()) {
          rval = (time_t)-1;
      }
      rval = realtime(tloc);
  } else {
      if (tloc) {
          *tloc = faketime;
      }
      rval = faketime;
  }

  return rval;
}

static inline void initialize_time_stub(void)
{
  use_realtime = 1;
  time(&faketime);
  use_realtime = 0;
}

static inline void sumtime(time_t add)
{
  faketime += add;
}

/* DSMESOCK STUB */

GSList *dsmesock_broadcasts = NULL;

static inline void initialize_dsmesock_stub(void)
{
  assert(dsmesock_broadcasts == NULL);
//  expected_dsmesock_broadcasts = g_ptr_array_new_with_free_func(free);
}

static inline void cleanup_dsmesock_stub(void)
{
  g_slist_free_full(dsmesock_broadcasts, free);
}

#define queued_dsmesock(T) ((T*)queued_dsmesock_(DSME_MSG_ID_(T), #T))
static inline void* queued_dsmesock_(unsigned type, const char* name)
{
  queued_msg_t* container = 0;
  dsmemsg_generic_t* msg = 0;

  assert(dsmesock_broadcasts);

  container = dsmesock_broadcasts->data;
  msg = container->data;
  assert(msg->type_ == type);

  free(container);
  dsmesock_broadcasts = g_slist_delete_link(dsmesock_broadcasts, dsmesock_broadcasts);
  return msg;
}

void dsmesock_broadcast(const void* msg)
{

  queued_msg_t*      newmsg;
  dsmemsg_generic_t* genmsg = (dsmemsg_generic_t*)msg;

  if (!msg) return;
  if (genmsg->line_size_ < sizeof(dsmemsg_generic_t)) return;

  newmsg = (queued_msg_t*)malloc(sizeof(queued_msg_t));
  if (!newmsg) return;

  newmsg->data = (dsmemsg_generic_t*)malloc(genmsg->line_size_);
  if (newmsg->data) {
      memcpy(newmsg->data, genmsg, genmsg->line_size_);
      // TODO: perhaps use GQueue for faster appending?
      dsmesock_broadcasts = g_slist_append(dsmesock_broadcasts, newmsg);
  } else {
      free(newmsg);
      newmsg = NULL;
  }

}

/* TEST DRIVER */
#include "testdriver.h"


#define ALARM_STATE_DIR      "/var/lib/dsme"
#define ALARM_STATE_FILE     ALARM_STATE_DIR "/alarm_queue_status"
/* HELPERS */
static gchar* original_alarm_queue = NULL;
static void set_alarm_queue(long int alarmtime)
{
  assert(original_alarm_queue == NULL);

  g_file_get_contents(ALARM_STATE_FILE,
                      &original_alarm_queue,
                      NULL,
                      NULL);

  if (alarmtime != -1) {
      gchar* alarmdata = g_strdup_printf("%ld", alarmtime);

      assert(g_mkdir_with_parents(ALARM_STATE_DIR, 0755) == 0);

      gboolean success = g_file_set_contents(ALARM_STATE_FILE,
                          alarmdata,
                          -1,
                          NULL);
      g_free(alarmdata);
      alarmdata = NULL;
      assert(success);
  } else {
      g_unlink(ALARM_STATE_FILE);
  }
}

static void reset_alarm_queue(void)
{
  if (original_alarm_queue) {
      assert(g_mkdir_with_parents(ALARM_STATE_DIR, 0755) == 0);

      gboolean success = g_file_set_contents(ALARM_STATE_FILE,
                          original_alarm_queue,
                          -1,
                          NULL);
      assert(success);

      g_free(original_alarm_queue);
      original_alarm_queue = NULL;
  } else {
      g_unlink(ALARM_STATE_FILE);
  }
}

static void initialize(void)
{
  if (!dsme_log_open(LOG_METHOD_STDOUT, 7, false, "    ", 0, 0, "")) {
      fatal("dsme_log_open() failed");
  }

  initialize_dbus_stub();
  initialize_dsmesock_stub();
  initialize_time_stub();

}

static void finalize(void)
{
  cleanup_dbus_stub();
  cleanup_dsmesock_stub();
}

static module_t* alarmtracker_module = NULL;
static void load_alarmtracker(long int alarmtime)
{
  set_alarm_queue(alarmtime);
  gchar* module_name_tmp = g_strconcat(dsme_module_path, "alarmtracker.so", NULL);
  alarmtracker_module = load_module_under_test(module_name_tmp);

  DSM_MSGTYPE_DBUS_CONNECT msg = TEST_MSG_INIT(DSM_MSGTYPE_DBUS_CONNECT);
  send_message(alarmtracker_module, &msg);
  g_free(module_name_tmp);
}

static void unload_alarmtracker(void)
{
  DSM_MSGTYPE_DBUS_DISCONNECT msg = TEST_MSG_INIT(DSM_MSGTYPE_DBUS_DISCONNECT);
  send_message(alarmtracker_module, &msg);
  unload_module_under_test(alarmtracker_module);
  reset_alarm_queue();
  assert(g_slist_length(dbus_signal_bindings)==0);
}

/* TEST CASES */

static void test_init_noqueuefile(void)
{
  load_alarmtracker(-1);

  assert(message_queue_is_empty());
  assert(!timer_exists());

  assert(dbusmsgq_async->len == 0);
  assert(dbusmsgq_blocking->len == 0);

  unload_alarmtracker();
}

static void test_init_noalarm(void)
{
  load_alarmtracker(0);

  assert(message_queue_is_empty());
  assert(!timer_exists());

  assert(dbusmsgq_async->len == 0);
  assert(dbusmsgq_blocking->len == 0);

  unload_alarmtracker();
}

static void test_init_activealarm(void)
{
  load_alarmtracker(1);

  /* INTERNAL MSGs */
  DSM_MSGTYPE_SET_ALARM_STATE *msg;
  assert((msg = queued(DSM_MSGTYPE_SET_ALARM_STATE)));
  assert(msg->alarm_set);
  free(msg);

  assert(message_queue_is_empty());
  assert(!timer_exists());

  /* DBUS IF */
  assert(dbusmsgq_async->len == 0);
  assert(dbusmsgq_blocking->len == 0);

  /* DSMESOCK */
  assert((msg = queued_dsmesock(DSM_MSGTYPE_SET_ALARM_STATE)));
  assert(msg->alarm_set);
  free(msg);
  assert(g_slist_length(dsmesock_broadcasts)==0);

  unload_alarmtracker();
}

/* 10sec < DSME SNOOZE TIMEOUT */
static void test_init_alarm_in10sec(void)
{
  load_alarmtracker(time(0)+10);

  /* INTERNAL MSGs */
  DSM_MSGTYPE_SET_ALARM_STATE *msg;
  assert((msg = queued(DSM_MSGTYPE_SET_ALARM_STATE)));
  assert(msg->alarm_set);
  free(msg);

  assert(message_queue_is_empty());
  assert(!timer_exists());

  /* DBUS IF */
  assert(dbusmsgq_async->len == 0);
  assert(dbusmsgq_blocking->len == 0);

  /* DSMESOCK */
  assert((msg = queued_dsmesock(DSM_MSGTYPE_SET_ALARM_STATE)));
  assert(msg->alarm_set);
  free(msg);
  assert(g_slist_length(dsmesock_broadcasts)==0);

  unload_alarmtracker();
}

/* 300sec > DSME SNOOZE TIMEOUT */
static void test_init_alarm_in5min(void)
{
  load_alarmtracker(time(0)+300);

  /* INTERNAL MSGs */
  DSM_MSGTYPE_SET_ALARM_STATE *msg;
  assert(message_queue_is_empty());

  /* DBUS IF */
  assert(dbusmsgq_async->len == 0);
  assert(dbusmsgq_blocking->len == 0);

  /* DSMESOCK */
  assert((msg = queued_dsmesock(DSM_MSGTYPE_SET_ALARM_STATE)));
  assert(msg->alarm_set);
  free(msg);
  assert(g_slist_length(dsmesock_broadcasts)==0);

  assert(timer_exists());
  sumtime(first_timer_seconds());
  trigger_timer();

  assert(!message_queue_is_empty());
  assert((msg = queued(DSM_MSGTYPE_SET_ALARM_STATE)));
  assert(msg->alarm_set);
  free(msg);
  assert(dbusmsgq_async->len == 0);
  assert(dbusmsgq_blocking->len == 0);
  assert(g_slist_length(dsmesock_broadcasts)==0);

  assert(!timer_exists());

  unload_alarmtracker();
}

/* Set alarm with com.nokia.time dbus interface */
static void test_init_set_alarm_in5min(void)
{
  load_alarmtracker(0);

  DBusMessage* alarm_setup_msg = dbus_message_new_signal("/com/nokia/time", "com.nokia.time", "next_bootup_event");

  time_t new_alarm_time = time(0)+300;
  dbus_message_append_args(alarm_setup_msg,
                           DBUS_TYPE_INT32, &new_alarm_time);

  dsme_dbus_stub_send_signal(alarm_setup_msg);
  free(alarm_setup_msg);

  assert(!message_queue_is_empty());
  assert(queued(DSM_MSGTYPE_WAIT));
  assert(message_queue_is_empty());

  DSM_MSGTYPE_WAKEUP wakeupmsg =
      TEST_MSG_INIT(DSM_MSGTYPE_WAKEUP);
  send_message(alarmtracker_module, &wakeupmsg);


  /* INTERNAL MSGs */
  DSM_MSGTYPE_SET_ALARM_STATE *msg;
  assert(message_queue_is_empty());

  /* DBUS IF */
  assert(dbusmsgq_async->len == 0);
  assert(dbusmsgq_blocking->len == 0);

  /* DSMESOCK */
  assert((msg = queued_dsmesock(DSM_MSGTYPE_SET_ALARM_STATE)));
  assert(msg->alarm_set);
  free(msg);
  assert(g_slist_length(dsmesock_broadcasts)==0);

  assert(timer_exists());
  sumtime(first_timer_seconds());
  trigger_timer();

  assert(!message_queue_is_empty());
  assert((msg = queued(DSM_MSGTYPE_SET_ALARM_STATE)));
  assert(msg->alarm_set);
  free(msg);
  assert(dbusmsgq_async->len == 0);
  assert(dbusmsgq_blocking->len == 0);
  assert(g_slist_length(dsmesock_broadcasts)==0);

  assert(!timer_exists());

  unload_alarmtracker();
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


  run(test_init_noqueuefile);
  run(test_init_noalarm);
  run(test_init_activealarm);
  run(test_init_alarm_in10sec);
  run(test_init_alarm_in5min);
  run(test_init_set_alarm_in5min);

  finalize();

  fprintf(stderr, "\n[* * *   S U C C E S S   * * *]\n\n");

  return EXIT_SUCCESS;
}

