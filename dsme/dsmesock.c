/**
   @file dsmesock.c

   This file implements DSME side of dsme socket operations.
   <p>
   Copyright (C) 2004-2010 Nokia Corporation.

   @author Ari Saastamoinen
   @author Semi Malinen <semi.malinen@nokia.com>
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

#ifndef __cplusplus
#define _GNU_SOURCE
#endif

#include "../include/dsme/dsmesock.h"
#include "../include/dsme/logging.h"
#include "../include/dsme/modulebase.h"
#include <dsme/protocol.h>

#include <glib.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <syslog.h>


static gboolean accept_client(GIOChannel*  source,
                              GIOCondition condition,
                              gpointer     p);
static gboolean handle_client(GIOChannel*  source,
                              GIOCondition condition,
                              gpointer     conn);
static void close_client(dsmesock_connection_t* conn);
static void add_client(dsmesock_connection_t* conn);
static void remove_client(dsmesock_connection_t* conn);



/* List of all connections made to listening socket */
static GSList* clients = 0;

/* Listening socket fd */
static GIOChannel*        as_chan          =  0;
static dsmesock_callback* read_and_queue_f =  0;


/*
 * Initialize listening socket and static variables
 * Return 0 on OK, -1 on error.
 */
int dsmesock_listen(dsmesock_callback* read_and_queue)
{
  const char*        dsmesock_filename = NULL;
  int                fd;
  struct sockaddr_un laddr;

  dsmesock_filename = getenv("DSME_SOCKFILE");
  if (dsmesock_filename == 0 || *dsmesock_filename == '\0') {
      dsmesock_filename = dsmesock_default_location;
  }

  memset(&laddr, 0, sizeof(laddr));
  laddr.sun_family = AF_UNIX;
  strncpy(laddr.sun_path, dsmesock_filename, sizeof(laddr.sun_path) - 1);
  laddr.sun_path[sizeof(laddr.sun_path) - 1] = 0;

  fd = socket(PF_UNIX, SOCK_STREAM, 0);
  if (fd == -1) {
      goto fail;
  }

  if(fcntl(fd, F_SETFL, O_NONBLOCK) == -1) {
      goto close_and_fail;
  }

  unlink(dsmesock_filename);
  if(bind(fd, (struct sockaddr *)&laddr, sizeof(laddr)) == -1) {
      goto close_and_fail;
  }
  chmod(dsmesock_filename, 0646);

  if(listen(fd, 1) == -1) {
      goto close_and_fail;
  }

  if (!(as_chan = g_io_channel_unix_new(fd))) {
    goto close_and_fail;
  }
  if (!g_io_add_watch(as_chan, G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL,
		      accept_client, as_chan)) {
    goto close_channel_and_fail;
  }

  read_and_queue_f = read_and_queue;

  return 0;

close_channel_and_fail:
  g_io_channel_shutdown(as_chan, FALSE, 0);
  g_io_channel_unref(as_chan);
  as_chan = 0;
  goto fail;

close_and_fail:
  close(fd);
fail:
  return -1;
}

static gboolean accept_client(GIOChannel*  source,
                              GIOCondition condition,
                              gpointer     p)
{
  int                    opt;
  socklen_t              optlen;
  int                    newfd;
  dsmesock_connection_t* newconn     = 0;

  if( condition & (G_IO_ERR | G_IO_HUP | G_IO_NVAL) ) {
    dsme_log(LOG_CRIT, "disabling client connect watcher");
    return FALSE;
  }

  newfd = accept(g_io_channel_unix_get_fd(source), 0, 0);
  if(newfd == -1) return TRUE;

  newconn = dsmesock_init(newfd);
  if(newconn == 0) {
    close(newfd);
    return TRUE;
  }

  opt    = 1;
  optlen = sizeof(opt);

  setsockopt(newfd, SOL_SOCKET, SO_PASSCRED, &opt, optlen);
  /* If that fails it is not fatal */

  optlen = sizeof(newconn->ucred);
  if(getsockopt(newfd, SOL_SOCKET, SO_PEERCRED,
                &newconn->ucred, &optlen) == -1)
  {
    /* if that fails, fill some bogus values */
    newconn->ucred.pid =  0;
    newconn->ucred.uid = -1;
    newconn->ucred.gid = -1;
  }

  if (!(newconn->channel = g_io_channel_unix_new(newfd)) ||
      !g_io_add_watch(newconn->channel,
		      G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL,
                      handle_client,
                      newconn))
  {
    close_client(newconn);
  } else {
    add_client(newconn);
  }

  return TRUE; /* do not discard the listening channel */
}


static gboolean handle_client(GIOChannel*  source,
                              GIOCondition condition,
                              gpointer     conn)
{
  bool keep_connection = true;

  if (condition & G_IO_IN) {
      if (read_and_queue_f) {
          keep_connection = read_and_queue_f(conn);
      } else {
          keep_connection = false;
      }
  }
  if (condition & (G_IO_ERR | G_IO_HUP | G_IO_NVAL)) {
      keep_connection = false;
  }

  if (!keep_connection) {
      close_client(conn);
  }

  return keep_connection;
}

static void close_client(dsmesock_connection_t* conn)
{
  if (conn) {
      remove_client(conn);

      if (conn->channel) {
          /* the channel does not own the socket fd (dsmesock does),
           * so don't do g_io_channel_shutdown();
           * instead, unset close on unref.
           */
          g_io_channel_set_close_on_unref(conn->channel, FALSE);
          g_io_channel_unref(conn->channel);
          conn->channel = 0;
      }
      dsmesock_close((dsmesock_connection_t*)conn);
  }
}


static void add_client(dsmesock_connection_t* conn)
{
  clients = g_slist_prepend(clients, conn);
}

static void remove_client(dsmesock_connection_t* conn)
{
  GSList* node = g_slist_find(clients, conn);

  if (node) {
    clients = g_slist_delete_link(clients, node);
  }
}

/*
 * Close listening socket
 * Close all client sockets
 */
void dsmesock_shutdown(void)
{
  if (as_chan) {
    // TODO: no strong guarantee that as_chan is unique
    g_source_remove_by_user_data(as_chan);
    g_io_channel_shutdown(as_chan, FALSE, 0);
    g_io_channel_unref(as_chan);
    as_chan = 0;
  }

  while (clients) {
    close_client(clients->data);
  }
}
