/**
   @file modulebase.h

   DSME plugin framework types and functions.
   <p>
   Copyright (C) 2004-2010 Nokia Corporation.

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

#ifndef DSME_MODULEBASE_H
#define DSME_MODULEBASE_H

#include "modules.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct _GSList; // TODO: ugly forward declaration due to broken glib.h

/**
   Initializes DSME module framework.

   This function should be called before calling any other DSME module
   framework functions.

   @return 0 on success, non-0 on error.
*/
bool modulebase_init(const struct _GSList* module_names);

int modulebase_shutdown(void);

/**
   Loads a DSME module.

   Module specified by @c filename parameter is loaded. If the path is not
   absolute, loading is first tried with @c ./ prepended to the filename.
   If module exports symbol @c module_init, it is assumed to be a function of
   type @c module_init_fn_t and called. If module exports symbol
   @c message_handlers, it is assumed to be an array of @c module_funcmap_t
   structures describing messages handled by module.

   @param filename	Filename of the module to be loaded
   @param priority	Priority of this module

   @return Module handle, or NULL pointer if loading was unsuccessful.
*/
module_t* load_module(const char* filename, int priority);


/**
   Unloads module.

   Module specified by @c module is unloaded. If the module exports symbol
   @c module_fini, it is assumed to be function of type @c module_fini_fn_t
   and called before module is unloaded.

   @param module  Module handle returned by call to load_module.
   @return On success, true; on error false.
*/
bool unload_module(module_t* module);

/**
   Passes messages in queue to message handlers.
*/
void process_message_queue(void);

const module_t* current_module(void);
void enter_module(const module_t* module);
void leave_module(void);

/**
   Adds a message to list of handlers

   @param msg_type  Type of the message to be registered
   @param callback Function to be called when given msg_type is handled
   @param owner    Pointer to the module module who owns this callback
   @return 0 on OK, -1 on error
*/
int add_single_handler(u_int32_t       msg_type,
                       size_t          msg_size,
                       handler_fn_t*   callback,
                       const module_t* owner);

/**
   Wrapper to make dsme process exit cleanly
*/
void dsme_exit(int exit_code);

enum {
    DSME_MSG_ENUM(DSM_MSGTYPE_IDLE, 0x00001337),
};

typedef dsmemsg_generic_t DSM_MSGTYPE_IDLE;

#ifdef __cplusplus
}
#endif

#endif
