/**
   @file dsmesock.h

   DSME socket function prototypes and structures.
   <p>
   Copyright (C) 2004-2009 Nokia Corporation.

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


#ifndef DSMESOCK_H
#define DSMESOCK_H

#include <sys/select.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct dsmesock_connection_t;

  /**
   * @defgroup dsmesock DSMEsock library
   * @{
   */

  /**
   * @defgroup dsmesock_server DSMEsock server interface
   * @ingroup dsmesock
   * @{
   */


/**
   A callback for client sockets ready to be read.

   @param conn   a ready socket
   @return false if the socket should be closed; true otherwise
*/
typedef bool dsmesock_callback(struct dsmesock_connection_t* conn);

/**
   Initialize listening socket and static variables

   @return 0 on OK, -1 on error.
*/
int dsmesock_listen(dsmesock_callback* read_and_queue);


/**
   Close listening socket
   Close all client sockets

   @return nothing
*/
void dsmesock_shutdown(void);


  /**
   * @}
   * @}
   */

#ifdef __cplusplus
}
#endif


#endif
