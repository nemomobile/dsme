/**
   @file modules.h

   DSME interface towards plugin modules.
   <p>
   Copyright (C) 2004-2010 Nokia Corporation.

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

#ifndef DSME_MODULES_H
#define DSME_MODULES_H

#include <dsme/messages.h>
#include <sys/types.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
   @defgroup module_if Module interface
*/

#define DSME_HANDLER(T, SENDER, MSG) \
  static void T ## _HANDLER2_(endpoint_t* SENDER, const T* MSG); \
  static void T ## _HANDLER1_(endpoint_t* conn, const dsmemsg_generic_t* msg) \
  { \
    /*log("--> " #T);*/ \
    T ## _HANDLER2_(conn, (const T*)msg); \
    /*log(#T "() --> %d", r);*/ \
  } \
  static void T ## _HANDLER2_(endpoint_t* SENDER, const T* MSG)

#define DSME_HANDLER_BINDING(T) \
  { DSME_MSG_ID_(T), T ## _HANDLER1_, sizeof(T) }


typedef struct endpoint_t endpoint_t;

struct dsmesock_connection_t; // TODO: remove

/**
   Message handler type.
*/
typedef void (handler_fn_t)(endpoint_t* sender, const dsmemsg_generic_t* msg);


/**
   Handler information entry in module.
*/
typedef struct {
    u_int32_t     msg_type;
    handler_fn_t* callback;
    size_t        msg_size;
} module_fn_info_t;


/**
   Module initialization function type.
   @ingroup module_if
*/
typedef struct module_t module_t;
typedef void (module_init_fn_t)(module_t*); // TODO: const module_t*

/**
   Module shutdown function type.
   @ingroup module_if
*/
typedef void (module_fini_fn_t)(void);

extern module_init_fn_t module_init;
extern module_fini_fn_t module_fini;

extern const char* module_name(const module_t* module);

/**
   Queues a message for handling. Use this to send messages from modules to modules.
   Does not free the message.

   @ingroup message_if
   @param msg	Message to be queued
   @return 0
*/
void broadcast_internally_with_extra(const void* msg,
                                     size_t      extra_size,
                                     const void* extra);
void broadcast_internally(const void* msg);
void broadcast_internally_from_socket(const void*                   msg,
                                      struct dsmesock_connection_t* conn);
void broadcast_with_extra(const void* msg,
                          size_t      extra_size,
                          const void* extra);
void broadcast(const void* msg);

void endpoint_send_with_extra(endpoint_t* recipient,
                              const void* msg,
                              size_t      extra_size,
                              const void* extra);
void endpoint_send(endpoint_t* recipient, const void* msg);

const struct ucred* endpoint_ucred(const endpoint_t* sender);
char* endpoint_name_by_pid(pid_t pid);
char* endpoint_name(const endpoint_t* sender);
bool endpoint_same(const endpoint_t* a, const endpoint_t* b);
bool endpoint_is_dsme(const endpoint_t* endpoint);
endpoint_t* endpoint_copy(const endpoint_t* endpoint);
void endpoint_free(endpoint_t* endpoint);


#ifdef __cplusplus
}
#endif

#endif
