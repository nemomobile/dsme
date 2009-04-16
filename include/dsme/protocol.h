/**
   @file protocol.h

   This file defines needed structures and prototypes of needed function
   for using dsme socket.
   <p>
   Copyright (C) 2004-2009 Nokia Corporation.

   @author Ari Saastamoinen
   @author Tuukka Tikkanen
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

#ifndef DSME_PROTOCOL_H
#define DSME_PROTOCOL_H

/*
 * some glibc versions seems to mistakenly define ucred behind __USE_GNU;
 * work around by #defining _GNU_SOURCE
 */
#ifndef __cplusplus
#define _GNU_SOURCE
#endif

#include <sys/socket.h>
#include <sys/un.h>

#ifndef EXTERN_C
#ifdef __cplusplus
#define EXTERN_C extern "C"
#else
#define EXTERN_C
#endif
#endif


/**
 * @defgroup dsmesock DSMEsock library
 */

/**
 * @defgroup dsmesock_client DSMEsock client interface
 * @ingroup dsmesock
 */

// TODO: ugly forward declaration due to broken glib.h;
//       remove it when dsmesock_connection_t is made opaque
//       (see arch ticket #230:
//        https://maemo.research.nokia.com/archtool/ticket/230)
struct _GIOChannel;

/**
   DSME socket internal information.
   @ingroup dsmesock_client
*/
typedef struct dsmesock_connection_t {
  int                 is_open;
  int                 fd;
  unsigned char*      buf;
  unsigned long       bufsize;
  unsigned long       bufused;
  struct ucred        ucred;
  struct _GIOChannel* channel;
} dsmesock_connection_t;


/*
 * Function prototypes
 */

/**
   Creates connection to DSME. The client opening a socket must be ready to receive messages
   from the socket e.g. using a select() and then dsmesock_receive() when data is available.
   The received message must be also free()'ed after use.
   @ingroup dsmesock_client
   @return pointer to connection structure.
*/
EXTERN_C dsmesock_connection_t*  dsmesock_connect(void);

/**
   Creates connection structure for an existing socket. The socket is put to
   nonblocking mode.
   @ingroup dsmesock_client
   @param fd	File handle for the socket.
   @return connection structure.
*/
EXTERN_C dsmesock_connection_t*  dsmesock_init(int fd);

/**
   Receives data from connection.
   @ingroup dsmesock_client
   @param conn	Connection to be read.
   @return pointer to received message.
*/
EXTERN_C void* dsmesock_receive(dsmesock_connection_t* conn);


/**
   Sends message to an other end of the dsmesock connection. Does not free the message.

   @ingroup dsmesock_client
   @param conn	Destination connection.
   @param msg	Pointer to message to be sent.
   @return Number of bytes sent, or -1 on error.
*/
EXTERN_C int dsmesock_send(dsmesock_connection_t* conn, const void* msg);

EXTERN_C int dsmesock_send_with_extra(dsmesock_connection_t* conn,
                                      const void*            msg,
                                      size_t                 extra_size,
                                      const void*            extra);


/**
   Sends message to all dsmesock client connections.
   @ingroup message_if
   @param msg  Pointer to message to be sent.
*/
EXTERN_C void dsmesock_broadcast(const void* msg);

EXTERN_C void dsmesock_broadcast_with_extra(const void* msg,
                                            size_t      extra_size,
                                            const void* extra);

/**
   Closes connection
   @ingroup dsmesock_client
   @param conn  Connection to be closed.
*/
EXTERN_C void dsmesock_close(dsmesock_connection_t* conn);


/**
   Retrieves peer credentials of the connection.
   @ingroup dsmesock_client
   @param conn  Connection
   @return pointer to @c ucred structure.
*/
EXTERN_C const struct ucred* dsmesock_getucred(dsmesock_connection_t* conn);


#endif
