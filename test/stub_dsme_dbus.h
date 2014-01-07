/**
   @file stub_dsme_dbus.h

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


#ifndef DSME_TEST_STUB_DSMEDBUS_H
#define DSME_TEST_STUB_DSMEDBUS_H

/*
  STUB for dsme_dbus
*/
#include <dbus/dbus.h>

/* storage for message bindings */
static GSList* dbus_signal_bindings;

struct DsmeDbusMessage {
  DBusConnection* connection;
  DBusMessage*    msg;
  DBusMessageIter iter;
};

void dsme_dbus_unbind_signals(bool* really_bound,
                              const dsme_dbus_signal_binding_t* bindings)
{
  if (really_bound && *really_bound) {
      const dsme_dbus_signal_binding_t* binding = bindings;

      while (binding && binding->handler) {
          GSList* signal_list;

          for (signal_list = dbus_signal_bindings; signal_list; signal_list = signal_list->next) {
              dsme_dbus_signal_binding_t* stored = signal_list->data;

              if (stored->handler == binding->handler &&
                  (strcmp(stored->interface, binding->interface) == 0) &&
                  (strcmp(stored->name, binding->name) == 0) ) {
                  dbus_signal_bindings = g_slist_delete_link(dbus_signal_bindings, signal_list);
              }
          }
          ++binding;
      }
      *really_bound = false;
  }
}


void dsme_dbus_bind_signals(bool*                             bound_already,
                            const dsme_dbus_signal_binding_t* bindings)
{
  if (bound_already && !*bound_already) {
      const dsme_dbus_signal_binding_t* binding = bindings;

      while (binding && binding->handler) {
          dbus_signal_bindings = g_slist_prepend(dbus_signal_bindings, (gpointer)binding);

          ++binding;
      }

      *bound_already = true;
  }
}

static void message_iter_next(DBusMessageIter* iter)
{
  if (dbus_message_iter_has_next(iter)) {
      dbus_message_iter_next(iter);
  }
}

int dsme_dbus_message_get_int(const DsmeDbusMessage* msg)
{
  dbus_int32_t i = 0;

  if (msg) {
      // TODO: check type!
      dbus_message_iter_get_basic((DBusMessageIter*)&msg->iter, &i);
      message_iter_next((DBusMessageIter*)&msg->iter);
  }

  return i;
}

const char* dsme_dbus_message_get_string(const DsmeDbusMessage* msg)
{
  const char* s = "";

  if (msg) {
      // TODO: check type!
      dbus_message_iter_get_basic((DBusMessageIter*)&msg->iter, &s);
      message_iter_next((DBusMessageIter*)&msg->iter);
  }

  return s;
}

static inline void dsme_dbus_stub_send_signal(DBusMessage* signal_msg)
{
  GSList* item;
  DsmeDbusMessage* dsmesig = g_new(DsmeDbusMessage, 1);
  dsmesig->connection = 0;
  dsmesig->msg = signal_msg;
  dbus_message_iter_init(signal_msg, &dsmesig->iter);

  for (item = dbus_signal_bindings; item; item = item->next) {
      const dsme_dbus_signal_binding_t* binding = (const dsme_dbus_signal_binding_t*)item->data;

      printf("bin: %s - %s\n", binding->interface, binding->name);
      if (
          (strcmp(binding->interface, dbus_message_get_interface(signal_msg)) == 0) &&
          (strcmp(binding->name, dbus_message_get_member(signal_msg)) == 0) ) {
          binding->handler(dsmesig);
      }
  }

  g_free(dsmesig);
}

#endif /* DSME_TEST_STUB_DSMEDBUS_H */
