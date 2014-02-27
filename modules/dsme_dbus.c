/**
   @file dsme_dbus.c

   D-Bus C binding for DSME
   <p>
   Copyright (C) 2008-2010 Nokia Corporation.

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

// TODO: add D-Bus filtering

#define _BSD_SOURCE

#include "dsme_dbus.h"

#include "dsme/logging.h"
#include "dsme/modules.h"
#include "dsme/modulebase.h"
#include "dsme/state.h"

#include <glib.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>


static const char *dsme_dbus_get_type_name(int type)
{
    static const char *res = "UNKNOWN";
    switch( type )
    {
    case DBUS_TYPE_INVALID:     res = "INVALID";     break;
    case DBUS_TYPE_BYTE:        res = "BYTE";        break;
    case DBUS_TYPE_BOOLEAN:     res = "BOOLEAN";     break;
    case DBUS_TYPE_INT16:       res = "INT16";       break;
    case DBUS_TYPE_UINT16:      res = "UINT16";      break;
    case DBUS_TYPE_INT32:       res = "INT32";       break;
    case DBUS_TYPE_UINT32:      res = "UINT32";      break;
    case DBUS_TYPE_INT64:       res = "INT64";       break;
    case DBUS_TYPE_UINT64:      res = "UINT64";      break;
    case DBUS_TYPE_DOUBLE:      res = "DOUBLE";      break;
    case DBUS_TYPE_STRING:      res = "STRING";      break;
    case DBUS_TYPE_OBJECT_PATH: res = "OBJECT_PATH"; break;
    case DBUS_TYPE_SIGNATURE:   res = "SIGNATURE";   break;
    case DBUS_TYPE_UNIX_FD:     res = "UNIX_FD";     break;
    case DBUS_TYPE_ARRAY:       res = "ARRAY";       break;
    case DBUS_TYPE_VARIANT:     res = "VARIANT";     break;
    case DBUS_TYPE_STRUCT:      res = "STRUCT";      break;
    case DBUS_TYPE_DICT_ENTRY:  res = "DICT_ENTRY";  break;
    }
    return res;
}

static bool dsme_dbus_check_arg_type(DBusMessageIter* iter, int want_type)
{
    int have_type = dbus_message_iter_get_arg_type(iter);

    if( have_type == want_type )
	 return true;

    dsme_log(LOG_WARNING, "dbus message parsing failed: expected %s, got %s",
	     dsme_dbus_get_type_name(want_type),
	     dsme_dbus_get_type_name(have_type));
    return false;
}

static DBusHandlerResult
dsme_dbus_filter(DBusConnection *con, DBusMessage *msg, void *aptr)
{
    FILE* f;

    if( dbus_message_is_signal(msg, DBUS_INTERFACE_LOCAL, "Disconnected") ) {
      dsme_log(LOG_CRIT, "Disconnected from system bus; rebooting");
      /* mark failure and request reboot */
      if ((f = fopen(DBUS_FAILED_FILE, "w+")) != NULL)
	  fclose(f);
      DSM_MSGTYPE_REBOOT_REQ req = DSME_MSG_INIT(DSM_MSGTYPE_REBOOT_REQ);
      broadcast_internally(&req);
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static DBusConnection *dsme_dbus_try_to_connect(DBusError *err)
{
    static DBusConnection *con = 0;

    if( con )
	goto EXIT;

    if( !(con = dbus_bus_get(DBUS_BUS_SYSTEM, err)) )
	goto EXIT;

    dbus_connection_add_filter(con, dsme_dbus_filter, 0, 0);
    dbus_connection_set_exit_on_disconnect(con, FALSE);

EXIT:
    // NOTE: returns null or new reference
    return con ? dbus_connection_ref(con) : 0;
}

DBusConnection *dsme_dbus_get_connection(DBusError *error)
{
    DBusError       err = DBUS_ERROR_INIT;
    DBusConnection *con = dsme_dbus_try_to_connect(&err);

    if( !con ) {
	if( error )
	    dbus_move_error(&err, error);
	else
	    dsme_log(LOG_DEBUG, "dbus_bus_get(): %s\n", err.message);
    }
    dbus_error_free(&err);

    // NOTE: returns null or new reference
    return con;
}

bool dsme_dbus_is_available(void)
{
    bool            res = false;
    DBusConnection *con = 0;

    if( (con = dsme_dbus_try_to_connect(0)) ) {
	dbus_connection_unref(con);
	res = true;
    }

    return res;
}

struct DsmeDbusMessage {
  DBusConnection* connection;
  DBusMessage*    msg;
  DBusMessageIter iter;
};

DsmeDbusMessage* dsme_dbus_reply_new(const DsmeDbusMessage* request)
{
  DsmeDbusMessage* m = 0;

  if (request) {
      m = g_new(DsmeDbusMessage, 1);

      m->connection = dbus_connection_ref(request->connection);
      m->msg        = dbus_message_new_method_return(request->msg);

      dbus_message_iter_init_append(m->msg, &m->iter);
  }

  return m;
}

static void message_delete(DsmeDbusMessage* reply)
{
  dbus_message_unref(reply->msg);
  dbus_connection_unref(reply->connection);
  g_free(reply);
}

void dsme_dbus_message_append_string(DsmeDbusMessage* msg, const char* s)
{
  if (msg) {
      dbus_message_iter_append_basic(&msg->iter, DBUS_TYPE_STRING, &s);
      // TODO: log append errors
  }
}

void dsme_dbus_message_append_int(DsmeDbusMessage* msg, int i)
{
  if (msg) {
      dbus_message_iter_append_basic(&msg->iter, DBUS_TYPE_INT32, &i);
      // TODO: log append errors
  }
}

int dsme_dbus_message_get_int(const DsmeDbusMessage* msg)
{
  // FIXME: caller can't tell apart zero from error
  dbus_int32_t i = 0;

  if (msg) {
      // FIXME: why take const pointer if we're going to modify the content?
      DBusMessageIter *iter = (DBusMessageIter *)&msg->iter;
      if( dsme_dbus_check_arg_type(iter, DBUS_TYPE_INT32) ) {
	  dbus_message_iter_get_basic(iter, &i);
      }
      dbus_message_iter_next(iter);
  }

  return i;
}

const char* dsme_dbus_message_get_string(const DsmeDbusMessage* msg)
{
  // FIXME: caller can't tell apart empty string from error
  const char* s = "";

  if (msg) {
      // FIXME: why take const pointer if we're going to modify the content?
      DBusMessageIter *iter = (DBusMessageIter *)&msg->iter;
      if( dsme_dbus_check_arg_type(iter, DBUS_TYPE_STRING) ) {
	  dbus_message_iter_get_basic(iter, &s);
      }
      dbus_message_iter_next(iter);
  }

  return s;
}

bool dsme_dbus_message_get_bool(const DsmeDbusMessage* msg)
{
  // FIXME: caller can't tell apart FALSE from error
  dbus_bool_t b = FALSE;

  if (msg) {
      // FIXME: why take const pointer if we're going to modify the content?
      DBusMessageIter *iter = (DBusMessageIter *)&msg->iter;
      if( dsme_dbus_check_arg_type(iter, DBUS_TYPE_BOOLEAN) ) {
	  dbus_message_iter_get_basic(iter, &b);
      }
      dbus_message_iter_next(iter);
  }

  return b;
}

static void message_send_and_delete(DsmeDbusMessage* msg)
{
  // TODO: check for errors and log them
  dbus_connection_send(msg->connection, msg->msg, 0);
  dbus_connection_flush(msg->connection);

  message_delete(msg);
}


DsmeDbusMessage* dsme_dbus_signal_new(const char* path,
                                      const char* interface,
                                      const char* name)
{
  DsmeDbusMessage* s = 0;
  DBusError        error;
  DBusConnection*  connection;

  if (path && interface && name) {
      dbus_error_init(&error);

      // TODO: we only use the system bus
      if ((connection = dsme_dbus_get_connection(&error))) {
          s = g_new(DsmeDbusMessage, 1);

          s->connection = connection;
          s->msg        = dbus_message_new_signal(path, interface, name);

          dbus_message_iter_init_append(s->msg, &s->iter);
      }
      dbus_error_free(&error);
  }

  return s;
}

void dsme_dbus_signal_emit(DsmeDbusMessage* sig)
{
  if (sig) {
      message_send_and_delete(sig);
  }
}


typedef struct Dispatcher Dispatcher;

typedef bool CanDispatch(const Dispatcher* d, const DBusMessage* msg);
typedef void Dispatch(const Dispatcher* dispatcher,
                      DBusConnection*   connection,
                      DBusMessage*      msg);

struct Dispatcher {
  CanDispatch*    can_dispatch;
  Dispatch*       dispatch;
  union {
    DsmeDbusMethod*  method;
    DsmeDbusHandler* handler;
  } target;
  const char*     interface;
  const char*     name;
  const char*     rules;
  const module_t* module;
};

static bool method_dispatcher_can_dispatch(const Dispatcher*  d,
                                           const DBusMessage* msg)
{
  /* const-cast-away due to silly D-Bus interface */
  return dbus_message_is_method_call((DBusMessage*)msg, d->interface, d->name);
}

static dbus_bool_t dbus_bus_get_unix_process_id(DBusConnection* conn,
                                                const char*     name,
                                                pid_t*          pid)
{
  DBusMessage*  msg;
  DBusMessage*  reply;
  DBusError     err;
  dbus_uint32_t pid_arg;

  msg = dbus_message_new_method_call("org.freedesktop.DBus",
                                     "/org/freedesktop/DBus",
                                     "org.freedesktop.DBus",
                                     "GetConnectionUnixProcessID");
  if (!msg) {
      dsme_log(LOG_DEBUG, "Unable to allocate new message");
      return FALSE;
  }

  if (!dbus_message_append_args(msg,
                                DBUS_TYPE_STRING,
                                &name,
                                DBUS_TYPE_INVALID))
  {
      dsme_log(LOG_DEBUG, "Unable to append arguments to message");
      dbus_message_unref(msg);
      return FALSE;
  }

  // TODO: it is risky that we are blocking
  dbus_error_init(&err);
  reply = dbus_connection_send_with_reply_and_block(conn, msg, -1, &err);
  if (dbus_error_is_set(&err)) {
      dsme_log(LOG_DEBUG,
               "Sending GetConnectionUnixProcessID failed: %s",
               err.message);
      dbus_error_free(&err);
      dbus_message_unref(msg);
      return FALSE;
  }

  dbus_error_init(&err);
  dbus_message_get_args(reply,
                        &err,
                        DBUS_TYPE_UINT32,
                        &pid_arg,
                        DBUS_TYPE_INVALID);
  if (dbus_error_is_set(&err)) {
      dsme_log(LOG_DEBUG,
               "Getting GetConnectionUnixProcessID args failed: %s",
               err.message);
      dbus_error_free(&err);
      dbus_message_unref(msg);
      dbus_message_unref(reply);
      return FALSE;
  }

  *pid = pid_arg;

  dbus_message_unref(msg);
  dbus_message_unref(reply);

  return TRUE;
}

char* dsme_dbus_endpoint_name(const DsmeDbusMessage* request)
{
  char* name = 0;

  if (!request) {
      name = strdup("(null request)");
  } else {
      pid_t pid;
      char* sender = strdup(dbus_message_get_sender(request->msg));
      if (!dbus_bus_get_unix_process_id(request->connection, sender, &pid)) {
          name = strdup("(could not get pid)");
      } else {
          name = endpoint_name_by_pid(pid);
      }
      free(sender);
  }

  return name;
}

static void method_dispatcher_dispatch(const Dispatcher* dispatcher,
                                       DBusConnection*   connection,
                                       DBusMessage*      msg)
{
  DsmeDbusMessage  request = {
    dbus_connection_ref(connection), dbus_message_ref(msg)
  };
  DsmeDbusMessage* reply   = 0;

  enter_module(dispatcher->module);
  dispatcher->target.method(&request, &reply);
  leave_module();

  dbus_connection_unref(request.connection);
  dbus_message_unref(request.msg);

  if (reply) {
    message_send_and_delete(reply);
  }
}

static Dispatcher* method_dispatcher_new(DsmeDbusMethod* method,
                                         const char*     interface,
                                         const char*     name,
                                         const char*     rules)
{
  Dispatcher* dispatcher = g_new(Dispatcher, 1);

  dispatcher->can_dispatch  = method_dispatcher_can_dispatch;
  dispatcher->dispatch      = method_dispatcher_dispatch;
  dispatcher->target.method = method;
  // NOTE: we don't bother to strdup()
  dispatcher->interface     = interface;
  dispatcher->name          = name;
  dispatcher->rules         = rules;
  dispatcher->module        = current_module();

  return dispatcher;
}

static bool handler_dispatcher_can_dispatch(const Dispatcher*  d,
                                            const DBusMessage* msg)
{
  /* const-cast-away due to silly D-Bus interface */
  return dbus_message_is_signal((DBusMessage*)msg, d->interface, d->name);
}

static void handler_dispatcher_dispatch(const Dispatcher* dispatcher,
                                        DBusConnection*   connection,
                                        DBusMessage*      msg)
{
  DsmeDbusMessage ind = {
    dbus_connection_ref(connection), dbus_message_ref(msg)
  };

  dbus_message_iter_init(msg, &ind.iter);

  enter_module(dispatcher->module);
  dispatcher->target.handler(&ind);
  leave_module();

  dbus_connection_unref(ind.connection);
  dbus_message_unref(ind.msg);
}

static Dispatcher* handler_dispatcher_new(DsmeDbusHandler* handler,
                                          const char*      interface,
                                          const char*      name)
{
  Dispatcher* dispatcher = g_new(Dispatcher, 1);

  dispatcher->can_dispatch   = handler_dispatcher_can_dispatch;
  dispatcher->dispatch       = handler_dispatcher_dispatch;
  dispatcher->target.handler = handler;
  // NOTE: we don't bother to strdup()
  dispatcher->interface      = interface;
  dispatcher->name           = name;
  dispatcher->module         = current_module();

  return dispatcher;
}

// TODO: virtual dispatcher_delete()


// TODO: Maybe combine DispatcherList with Service?
//       Or better yet: provide dispatcher_list_init() instead of _new()
typedef struct DispatcherList {
  GSList* dispatchers;
} DispatcherList;

static DispatcherList* dispatcher_list_new()
{
  DispatcherList* list = g_new(DispatcherList, 1);

  list->dispatchers = 0;

  return list;
}

static void dispatcher_list_add(DispatcherList* list, Dispatcher* dispatcher)
{
  list->dispatchers = g_slist_prepend(list->dispatchers, dispatcher);
}

static bool dispatcher_list_dispatch(const DispatcherList* list,
                                     DBusConnection*       connection,
                                     DBusMessage*          msg)
{
  bool          dispatched = false;
  int           msg_type   = dbus_message_get_type(msg);
  Dispatcher*   d          = 0;
  const GSList* i;
  for (i = list->dispatchers; i; i = g_slist_next(i)) {
    d = i->data;
    if (d->can_dispatch(d, msg)) {
        d->dispatch(d, connection, msg);
        dispatched = true;

        /* Method calls should have only one handler.
	 * Stop after suitable one has been found. */
        if( msg_type == DBUS_MESSAGE_TYPE_METHOD_CALL )
           break;
    }
  }

  return dispatched;
}


typedef bool FilterMessageHandler(gpointer        child,
                                  DBusConnection* connection,
                                  DBusMessage*    msg);

typedef struct Filter {
  DBusConnection*       connection;
  void*                 child;
  FilterMessageHandler* handler;
} Filter;

static bool filter_handle_message(Filter* filter, DBusMessage* msg)
{
  switch (dbus_message_get_type(msg)) {

  case DBUS_MESSAGE_TYPE_METHOD_CALL:
    // TODO: add logging
  break;

  case DBUS_MESSAGE_TYPE_SIGNAL:
  break;

  case DBUS_MESSAGE_TYPE_ERROR:
  {
    DBusError error;

    dbus_error_init(&error);
    if (dbus_set_error_from_message(&error, msg)) {
        dsme_log(LOG_DEBUG,
                 "D-Bus: %s: %s",
                 dbus_message_get_error_name(msg),
                 error.message);
    } else {
        dsme_log(LOG_DEBUG, "D-Bus: could not get error message");
    }
    dbus_error_free(&error);
  }
  break;

  default:
    // IGNORE (silently ignore per D-Bus documentation)
    break;
  }

  return false;
}

static DBusHandlerResult filter_static_message_handler(
  DBusConnection* connection,
  DBusMessage*    msg,
  gpointer        filterp)
{
  DBusHandlerResult result = DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  Filter*           filter = filterp;

  if (filter->handler(filter->child, connection, msg) ||
      filter_handle_message(filter, msg))
  {
    /* It is ok to have multiple handlers for signals etc.
     * Only method calls should be marked as "handled" */
    if( dbus_message_get_type(msg) == DBUS_MESSAGE_TYPE_METHOD_CALL )
    {
      result = DBUS_HANDLER_RESULT_HANDLED;
    }
  }

  return result;
}

static Filter* filter_new(void* child, FilterMessageHandler* handler)
{
  DBusError       error;
  DBusConnection* connection;
  Filter*         filter = 0;

  dbus_error_init(&error);

  // TODO: we only use the system bus
  if ((connection = dsme_dbus_get_connection(&error)) == 0) {
    dsme_log(LOG_ERR, "system bus connect failed: %s", error.message);
    dbus_error_free(&error);
  } else {

    dbus_connection_setup_with_g_main(connection, 0);
    filter = g_new(Filter, 1);

    if (!dbus_connection_add_filter(connection,
                                    filter_static_message_handler,
                                    filter,
                                    0))
    {
      dsme_log(LOG_ERR, "dbus_connection_add_filter() failed");
      g_free(filter);
      filter = 0;
    } else {

      filter->connection = connection;
      filter->child      = child;
      filter->handler    = handler;

    }
  }

  return filter;
}

static void filter_delete(Filter* filter)
{
  /* TODO */
  /* remove the D-Bus filter */
}


typedef struct Service {
  Filter*         filter;
  const char*     name;
  DispatcherList* methods;
} Service;

static bool service_handle_message(gpointer        servicep,
                                   DBusConnection* connection,
                                   DBusMessage*    msg)
{
  Service*  service = servicep;
  bool      handled = false;

  if (dbus_message_get_type(msg) == DBUS_MESSAGE_TYPE_METHOD_CALL) {
    handled = dispatcher_list_dispatch(service->methods, connection, msg);
  }

  return handled;
}

static Service* service_new(const char* name)
{
  DBusError error;
  Filter*   filter  = 0;
  Service*  service = 0;

  dbus_error_init(&error);

  service = g_new(Service, 1);

  if (!(filter = filter_new(service, service_handle_message))) {
    g_free(service);
    service = 0;
  }

  if (service) {
    if (dbus_bus_request_name(filter->connection, name, 0, &error) !=
        DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER)
    {
      dsme_log(LOG_DEBUG, "dbus_request_name(): %s\n", error.message);
      dbus_error_free(&error);
      g_free(service);
      service = 0;
      filter_delete(filter);
      filter = 0;
    }
  }

  if (service) {
    service->filter  = filter;
    service->name    = name;
    service->methods = dispatcher_list_new();
  }

  return service;
}

// TODO; service_delete()

static void service_bind(Service*       service,
                         DsmeDbusMethod method,
                         const char*    interface,
                         const char*    name,
                         const char*    rules)
{
  dispatcher_list_add(service->methods,
                      method_dispatcher_new(method, interface, name, rules));
}

typedef struct Server {
  GData* services;
} Server;

static Server* server_new()
{
  Server* server = g_new(Server, 1);

  server->services = 0;

  return server;
}

static bool server_bind(Server*        server,
                        DsmeDbusMethod method,
                        const char*    service,
                        const char*    interface,
                        const char*    name,
                        const char*    rules)
{
  bool     bound = false;
  Service* s;

  if (!(s = g_datalist_get_data(&server->services, service))) {
    if ((s = service_new(service))) {
      g_datalist_set_data(&server->services, service, s);
    }
  }

  if (s) {
    service_bind(s, method, interface, name, rules);
    bound = true;
  }

  return bound;
}

static Server* server_instance()
{
  static Server* the_server = 0;

  if (!the_server) {
    the_server = server_new();
  }

  return the_server;
}


typedef struct Client {
  Filter*         filter;
  DispatcherList* handlers;
} Client;

static bool client_handle_message(gpointer        clientp,
                                  DBusConnection* connection,
                                  DBusMessage*    msg)
{
  Client* client  = clientp;
  bool    handled = false;

  if (dbus_message_get_type(msg) == DBUS_MESSAGE_TYPE_SIGNAL) {
    handled = dispatcher_list_dispatch(client->handlers, connection, msg);
  }

  return handled;
}

static bool client_bind(Client*          client,
                        DsmeDbusHandler* handler,
                        const char*      interface,
                        const char*      name)
{
  bool        bound        = false;
  const char* match_format = "type='signal', interface='%s', member='%s'";
  char*       match;
  DBusError   error;

  dispatcher_list_add(client->handlers,
                      handler_dispatcher_new(handler, interface, name));

  match = malloc(strlen(match_format) +
                 strlen(interface)    +
                 strlen(name) - 3);
  sprintf(match, match_format, interface, name);

  dbus_error_init(&error);
  dbus_bus_add_match(client->filter->connection, match, &error);
  free(match);

  if (dbus_error_is_set(&error)) {
      dsme_log(LOG_DEBUG, "dbus_bus_add_match(): %s", error.message);
      dbus_error_free(&error);
  } else {
      dsme_log(LOG_DEBUG, "bound handler for: %s, %s", interface, name);
      bound = true;
  }

  return bound;
}

static Client* client_new()
{
    Filter* filter = 0;
    Client* client = 0;

    client = g_new(Client, 1);

    if (!(filter = filter_new(client, client_handle_message))) {
        g_free(client);
        client = 0;
    }

    if (client) {
        client->filter   = filter;
        client->handlers = dispatcher_list_new();
    }

    return client;
}

static Client* client_instance()
{
  static Client* the_client = 0;

  if (!the_client) {
    the_client = client_new();
  }

  return the_client;
}


void dsme_dbus_bind_methods(bool*                      bound_already,
                            const dsme_dbus_binding_t* bindings,
                            const char*                service,
                            const char*                interface)
{
  if (bound_already && !*bound_already) {

      const dsme_dbus_binding_t* binding = bindings;

      while (binding && binding->method) {
          if (!server_bind(server_instance(),
                           binding->method,
                           service,
                           interface,
                           binding->name,
                           0))
            {
              dsme_log(LOG_ERR, "D-Bus binding for '%s' failed", binding->name);
              // TODO: roll back the ones that succeeded and break?
            }
          ++binding;
      }

      *bound_already = true;
  }
}

void dsme_dbus_unbind_methods(bool*                      really_bound,
                              const dsme_dbus_binding_t* bindings,
                              const char*                service,
                              const char*                interface)
{
  if (really_bound && *really_bound) {

      // TODO

      *really_bound = false;
  }
}


void dsme_dbus_bind_signals(bool*                             bound_already,
                            const dsme_dbus_signal_binding_t* bindings)
{
  if (bound_already && !*bound_already) {
      const dsme_dbus_signal_binding_t* binding = bindings;

      while (binding && binding->handler) {
          Client* client;

          if (!(client = client_instance())) {
              dsme_log(LOG_ERR,
                       "Could not create D-Bus client for '%s'",
                       binding->name);
              // TODO: roll back the ones that succeeded and break?
          } else if (!client_bind(client,
                                  binding->handler,
                                  binding->interface,
                                  binding->name))
          {
              dsme_log(LOG_ERR, "D-Bus binding for '%s' failed", binding->name);
              // TODO: roll back the ones that succeeded and break?
          }
          ++binding;
      }
  }
}

void dsme_dbus_unbind_signals(bool*                             really_bound,
                              const dsme_dbus_signal_binding_t* bindings)
{
  if (really_bound && *really_bound) {

      // TODO

      *really_bound = false;
  }
}
