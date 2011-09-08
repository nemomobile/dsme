/**
   @file stub_dbus.h

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


#ifndef DSME_TEST_STUB_DBUS_H
#define DSME_TEST_STUB_DBUS_H

#include <dbus/dbus.h>

GPtrArray *expected_registrations = NULL;
GPtrArray *actual_registrations = NULL;

GPtrArray *dbusmsgq_blocking = NULL;
GPtrArray *dbusmsgq_async = NULL;

void dbus_connection_flush(DBusConnection *connection __attribute__ ((unused))){}

dbus_bool_t dbus_connection_send(DBusConnection *connection __attribute__ ((unused)),
                                 DBusMessage    *message,
                                 dbus_uint32_t  *client_serial __attribute__ ((unused)))
{
  if (dbusmsgq_async) {
      assert(0);
      g_ptr_array_add(dbusmsgq_async, (gpointer)message);
  }
  return TRUE;
}

void dbus_bus_add_match (DBusConnection *connection __attribute__ ((unused)),
                         const char     *rule,
                         DBusError      *error __attribute__ ((unused)))
{
  if (actual_registrations)
        g_ptr_array_add(actual_registrations, g_strdup(rule));

}

static inline void initialize_dbus_stub(void)
{
  dbusmsgq_blocking = g_ptr_array_new_with_free_func(free);
  dbusmsgq_async = g_ptr_array_new_with_free_func(free);
}

static inline void cleanup_dbus_stub(void)
{
  g_ptr_array_free(dbusmsgq_blocking, TRUE);
  g_ptr_array_free(dbusmsgq_async, TRUE);
}

static inline void initialize_dbus_stub_registrations(void)
{
  expected_registrations = g_ptr_array_new();
  actual_registrations = g_ptr_array_new_with_free_func(g_free);
}

static inline void cleanup_dbus_stub_registrations()
{
  g_ptr_array_free(expected_registrations, TRUE);
  g_ptr_array_free(actual_registrations, TRUE);
}
#endif /* DSME_TEST_STUB_DBUS_H */
