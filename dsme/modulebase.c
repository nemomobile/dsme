/**
   @file modulebase.c

   Implements DSME plugin framework.
   <p>
   Copyright (C) 2004-2010 Nokia Corporation

   @author Ari Saastamoinen
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

#ifndef __cplusplus
#define _GNU_SOURCE
#endif

#include "dsme/modulebase.h"
#include "dsme/messages.h"
#include "dsme/protocol.h"
#include "dsme/logging.h"
#include "dsme/mainloop.h"

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <sys/types.h>
#include <unistd.h>


/**
   Loaded module information.
*/
struct module_t {
    char* name;
    int   priority;
    void* handle;
};


/**
   Registered handler information.
*/
typedef struct {
    u_int32_t       msg_type;
    size_t          msg_size;
    const module_t* owner;
    handler_fn_t*   callback;
} msg_handler_info_t;


/**
    Sender of a queued message.
*/
struct endpoint_t {
    const module_t*        module;
    dsmesock_connection_t* conn;
    struct ucred           ucred; // only valid when conn != 0
};


/**
   Queued message.
*/
typedef struct {
    endpoint_t         from;
    const module_t*    to;
    dsmemsg_generic_t* data;
} queued_msg_t;


static int add_msghandlers(module_t* module);

static void remove_msghandlers(module_t* module);

/** 
   Comparison function; matches messages with handlers by message type.

   This is callback function of GCompareFunc type.

   @param a  Handler to be compared
   @param b  Message type to be compared. Internally cast to integer.

   @return negative/zero/positive if a is smaller/equal/larger than b.
*/
static gint msg_comparator(gconstpointer a, gconstpointer b);


/**
   Comparison function; used to sort message handlers.

   This is callback function of GCompareFunc type.

   Message handlers are sorted primarily by message type in descending order.
   Handlers for same message type are sorted by priority in descending order.

   @param a   New handler to be added to the list of handlers.
   @param b   Existing handler in list of handlers.

   @return negative/zero/positive if a is smaller/equal/larger than b.
*/
static gint sort_comparator(gconstpointer a, gconstpointer b);

/** Type agnostic, overflow safe macro for comparing numeric values
 *
 * @param v1  1st number
 * @param v2  2nd number (of the same type as the 1st one)
 *
 * @return -1,0,+1 depending if v1 is smaller, equal or larger than v2
 */
#define compare(v1,v2) (((v1)>(v2))-((v1)<(v2)))


/**
   Passes a message to all matching message handlers.

   @param from   Sender of the message
   @param msg	 Message to be handled
   @return 0
*/
static int handle_message(endpoint_t*              from,
                          const module_t*          to,
                          const dsmemsg_generic_t* msg);


static GSList*     modules       = 0;
static GSList*     callbacks     = 0;
static GSList*     message_queue = 0;

static const struct ucred bogus_ucred = {
    .pid =  0,
    .uid = -1,
    .gid = -1
};

static int msg_comparator(gconstpointer a, gconstpointer b)
{
  const msg_handler_info_t* handler  = (msg_handler_info_t*)a;
  u_int32_t                 msg_type = GPOINTER_TO_UINT(b);

  return compare(handler->msg_type, msg_type);
}


static gint sort_comparator(gconstpointer a, gconstpointer b)
{
    const msg_handler_info_t* handler  = (msg_handler_info_t*)a;
    const msg_handler_info_t* existing = (msg_handler_info_t*)b;

    return compare(handler->msg_type, existing->msg_type) ?:
	   compare(handler->owner->priority, existing->owner->priority);
}


#ifdef OBSOLETE
/**
   Comparison function; match module by name

   @param node  Module node to be compared
   @param name  String to be compared
   @return 0 when name equas with node name, <>0 otherwise
*/
static int name_comparator(const module_t* node, const char* name)
{
    return !strcmp(node->name, name);
}
#endif


/**
   Add single hadler in message handlers list

   @param msg_type  Type of the message to be registered
   @param callback Function to be called when given msg_type is handled
   @param owner    Pointer to the module module who owns this callback
   @return 0 on OK, -1 on error
*/
int add_single_handler(u_int32_t       msg_type,
                       size_t          msg_size,
		       handler_fn_t*   callback,
	 	       const module_t* owner)
{
    msg_handler_info_t* handler = 0;

    handler = (msg_handler_info_t*)malloc(sizeof(msg_handler_info_t));
    if (!handler) {
        return -1;
    }
  
    handler->msg_type = msg_type;
    handler->msg_size = msg_size;
    handler->callback = callback;
    handler->owner    = owner;
  
    /* Insert into sorted list. */
    callbacks = g_slist_insert_sorted(callbacks,
				      handler,
				      sort_comparator);

    return 0;
}


/**
   Locates message handler table from module and adds all message handlers
   to global handler list.

   @param module	Pointer to the module to be initialised
   @return 0 when OK, or -1 on error
*/

static int add_msghandlers(module_t* module)
{
    module_fn_info_t* msg_handler_ptr;
  
    if (!module)
        return -1;
  
    for (msg_handler_ptr = (module_fn_info_t*)dlsym(module->handle,
                                                    "message_handlers");
	 msg_handler_ptr && msg_handler_ptr->callback;
	 msg_handler_ptr++)
    {
        if (add_single_handler(msg_handler_ptr->msg_type,
                               msg_handler_ptr->msg_size,
			       msg_handler_ptr->callback,
			       module))
            return -1;
    }

    return 0;
}


/**
   This function is called when some module is unloaded.
   This removes all registered message callbacks of gimes module.

   @param module Module whose callbacks will be removed
*/
static void remove_msghandlers(module_t* module)
{
    GSList* node;
    GSList* next;

    for (node = callbacks; node != 0; node = next) {
        next = g_slist_next(node);
        if (node->data &&
            ((msg_handler_info_t *)(node->data))->owner == module)
        {
            free(node->data);
            node->data = NULL;

            callbacks = g_slist_delete_link(callbacks, node);
        }
    }
}


static const module_t* currently_handling_module = 0;

const module_t* current_module(void)
{
    return currently_handling_module;
}

void enter_module(const module_t* module)
{
    currently_handling_module = module;
}

void leave_module()
{
    currently_handling_module = 0;
}


// TODO: all these returns and mallocs are a mess
static void queue_message(const endpoint_t* from,
                          const module_t*   to,
                          const void*       msg,
                          size_t            extra_size,
                          const void*       extra)
{
  queued_msg_t*      newmsg;
  dsmemsg_generic_t* genmsg = (dsmemsg_generic_t*)msg;

  if (!msg) return;
  if (genmsg->line_size_ < sizeof(dsmemsg_generic_t)) return;

  newmsg = (queued_msg_t*)malloc(sizeof(queued_msg_t));
  if (!newmsg) return;

  newmsg->data = (dsmemsg_generic_t*)malloc(genmsg->line_size_ + extra_size);
  if (newmsg->data) {
      memcpy(newmsg->data, genmsg, genmsg->line_size_);
      memcpy(((char*)newmsg->data)+genmsg->line_size_, extra, extra_size);
      newmsg->data->line_size_ += extra_size;

      newmsg->from = *from;
      newmsg->to   = to;

      // TODO: perhaps use GQueue for faster appending?
      message_queue = g_slist_append(message_queue, newmsg);
      return;
  }

  free(newmsg);
  newmsg = NULL;
}

void broadcast_internally_with_extra(const void* msg,
                                     size_t      extra_size,
                                     const void* extra)
{
  endpoint_t from = {
    .module = currently_handling_module,
    .conn   = 0,
    .ucred  = bogus_ucred
  };

  /* use 0 as recipient for broadcasting */
  queue_message(&from, 0, msg, extra_size, extra);
}

void broadcast_internally(const void* msg)
{
  broadcast_internally_with_extra(msg, 0, 0);
}

void broadcast_internally_from_socket(const void*            msg,
                                      dsmesock_connection_t* conn)
{
  endpoint_t from = {
    .module = 0,
    .conn   = conn,
  };

  const struct ucred* ucred = dsmesock_getucred(conn);
  if (ucred) {
      from.ucred = *ucred;
  } else {
      from.ucred = bogus_ucred;
  }

  /* use 0 as recipient for broadcasting */
  queue_message(&from, 0, msg, 0, 0);
}

void broadcast_with_extra(const void* msg, size_t extra_size, const void* extra)
{
  endpoint_t from = {
    .module = currently_handling_module,
    .conn   = 0,
    .ucred  = bogus_ucred
  };

  queue_message(&from, 0, msg, extra_size, extra);
  dsmesock_broadcast_with_extra(msg, extra_size, extra);
}

void broadcast(const void* msg)
{
  broadcast_with_extra(msg, 0, 0);
}

static void queue_for_module_with_extra(const module_t* recipient,
                                        const void*     msg,
                                        size_t          extra_size,
                                        const void*     extra)
{
  endpoint_t from = {
    .module = currently_handling_module,
    .conn   = 0,
    .ucred  = bogus_ucred
  };

  queue_message(&from, recipient, msg, extra_size, extra);
}

void endpoint_send_with_extra(endpoint_t* recipient,
                              const void* msg,
                              size_t      extra_size,
                              const void* extra)
{
  if (recipient) {
    if (recipient->module) {
      queue_for_module_with_extra(recipient->module, msg, extra_size, extra);
    } else if (recipient->conn) {
      dsmesock_send_with_extra(recipient->conn,   msg, extra_size, extra);
    } else {
      dsme_log(LOG_DEBUG, "endpoint_send(): no endpoint");
    }
  } else {
    dsme_log(LOG_DEBUG, "endpoint_send(): null endpoint");
  }
}

void endpoint_send(endpoint_t* recipient, const void* msg)
{
  endpoint_send_with_extra(recipient, msg, 0, 0);
}


const struct ucred* endpoint_ucred(const endpoint_t* sender)
{
  const struct ucred* u = 0;

  if (sender) {
    if (sender->module) {
      static struct ucred module_ucred;

      module_ucred.pid = getpid();
      module_ucred.uid = getuid();
      module_ucred.gid = getgid();

      u = &module_ucred;
    } else if (sender->conn) {
      u = &sender->ucred;
    }
  }

  if (!u) {
    u = &bogus_ucred;
  }

  return u;
}

char* endpoint_name_by_pid(pid_t pid)
{
  char* name = 0;

  char* filename;
  if (asprintf(&filename, "/proc/%u/cmdline", pid) != -1) {
      int   ret;
      FILE* f = fopen(filename, "r");
      free(filename);
      if (f) {
          char*  cmdline = 0;
          size_t n;
          ssize_t l = getline(&cmdline, &n, f);
          fclose(f);
          if (l > 0) {
              cmdline[l - 1] = '\0';
              ret = asprintf(&name, "pid %u: %s", pid, cmdline);
          } else {
              ret = asprintf(&name, "pid %u: (no name)", pid);
          }
          free(cmdline);
      } else {
          ret = asprintf(&name, "pid %u: (no such process)", pid);
      }

      if (ret == -1) {
          name = 0;
      }
  }

  return name;
}

char* endpoint_name(const endpoint_t* sender)
{
  char* name = 0;

  if (!sender) {
      name = strdup("(null endpoint)");
  } else if (!sender->conn) {
      name = strdup("dsme");
  } else {
      name = endpoint_name_by_pid(sender->conn->ucred.pid);
  }

  return name;
}

bool endpoint_same(const endpoint_t* a, const endpoint_t* b)
{
  bool same = false;

  if (a && b) {
    if ((a->module && a->module == b->module) ||
        (a->conn   && a->conn   == b->conn))
    {
      same = true;
    }
  }

  return same;
}

bool endpoint_is_dsme(const endpoint_t* endpoint)
{
    return (endpoint && endpoint->conn == 0);
}

endpoint_t* endpoint_copy(const endpoint_t* endpoint)
{
  endpoint_t* copy = 0;

  if (endpoint) {
    copy = malloc(sizeof(endpoint_t));

    if (copy) {
        copy->module = endpoint->module;
        copy->conn   = endpoint->conn;
    }
  }

  return copy;
}

void endpoint_free(endpoint_t* endpoint)
{
  free(endpoint);
}


void process_message_queue(void)
{
    while (message_queue) {
        queued_msg_t* front = (queued_msg_t*)message_queue->data;

        message_queue = g_slist_delete_link(message_queue,
                                            message_queue);
        handle_message(&front->from, front->to, front->data);
        free(front->data);
        free(front);
    }

    // send an IDLE message to indicate that the message queue is empty
    endpoint_t from = {
        .module = 0,
        .conn   = 0,
        .ucred  = bogus_ucred
    };
    DSM_MSGTYPE_IDLE idle = DSME_MSG_INIT(DSM_MSGTYPE_IDLE);
    handle_message(&from, 0, &idle);
}


static int handle_message(endpoint_t*              from,
                          const module_t*          to,
                          const dsmemsg_generic_t* msg)
{
  GSList*                   node;
  const msg_handler_info_t* handler;

  node = callbacks;
  while ((node = g_slist_find_custom(node,
                                     GUINT_TO_POINTER(dsmemsg_id(msg)),
                                     msg_comparator)))
  {
      handler = (msg_handler_info_t*)(node->data);
      if (handler && handler->callback) {
          if (!to || to == handler->owner) {
              if (msg->line_size_ >= handler->msg_size &&
                  msg->size_      == handler->msg_size)
              {
                  currently_handling_module = handler->owner;
                  handler->callback(from, msg);
                  currently_handling_module = 0;
              }
          }
      }
      node = g_slist_next(node);
  }

  return 0;
}


bool unload_module(module_t* module)
{
    bool    unloaded = false;
    GSList* node;

    if (module && (node = g_slist_find(modules, module))) {

        remove_msghandlers(module);

        if (module->handle) {
            /* Call module_fini() function if it exists */
            module_fini_fn_t* finifunc =
                (module_fini_fn_t *)dlsym(module->handle, "module_fini");

            if (finifunc) {
                currently_handling_module = module;
                finifunc();
                currently_handling_module = 0;
            }

            dlclose(module->handle);
        }

        if (module->name) {
            free(module->name);
            module->name = NULL;
        }

        free(module);
        module = NULL;

        modules = g_slist_delete_link(modules, node);

        unloaded = true;
    }

    return unloaded;
}


module_t* load_module(const char* filename, int priority)
{
    void*             dlhandle = 0;
    module_t*         module   = 0;
    module_init_fn_t* initfunc;

    /* Prepend ./ to non-absolute path */
    if (*filename != '/') {
        int     namelen;
        char *  newname;

        namelen = strlen(filename) + 3;
        newname = (char*)malloc(namelen);
        if (newname) {
            snprintf(newname, namelen, "./%s", filename);
            dlhandle = dlopen(newname, RTLD_NOW | RTLD_GLOBAL);
            free(newname);
        }
    }

    if (!dlhandle) {
        dlhandle = dlopen(filename, RTLD_NOW | RTLD_GLOBAL);
    }

    if (!dlhandle) {
        dsme_log(LOG_WARNING, "%s", dlerror());
        return 0;
    }

    /* Now the module should be open */

    module = (module_t*)malloc(sizeof(module_t));
    if (!module) {
        goto error;
    }

    module->handle = dlhandle;
    module->priority = priority;
    module->name = strdup(filename);
    if (!module->name) {
        goto error;
    }

    /* Call module_init() -function if it exists */
    initfunc = (module_init_fn_t *)dlsym(dlhandle, "module_init");
    if (initfunc) {
        currently_handling_module = module;
        initfunc(module);
        currently_handling_module = 0;
    }

    /* Add message handlers for the module */
    if(add_msghandlers(module)) {
        goto error;
    }

	/* Insert thee module to the modulelist */
    modules = g_slist_append(modules, module); /* Add module to list */

    return module;

error:
    dsme_log(LOG_WARNING, "%s", dlerror());
    if (module) {
        remove_msghandlers(module);
        free(module);
    }

    if (dlhandle) dlclose(dlhandle);
  
  return 0;
}


bool modulebase_init(const struct _GSList* module_names)
{
    const GSList* modname;

    for (modname = module_names; modname; modname = g_slist_next(modname)) {
        if (load_module((const char*)modname->data, 0) == NULL) {
            dsme_log(LOG_CRIT,
                     "Error loading start-up module: %s",
                     (const char*)modname->data);
            return false;
        }
    }

    return true;
}


const char* module_name(const module_t* module)
{
  return module->name;
}

int modulebase_shutdown(void)
{
        while (modules) {
		process_message_queue();
		unload_module((module_t*)modules->data);
	}

	process_message_queue();

	return 0;
}

void dsme_exit(int exit_code)
{
  dsme_main_loop_quit(exit_code);
}


