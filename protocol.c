/**
   @file protocol.c

   Implementation of DSME socket communication. 
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

#ifndef __cplusplus
#define _GNU_SOURCE
#endif

#include "dsme/protocol.h"
#include "dsme/messages.h"

#include <glib.h>
#include <sys/types.h>
#include <malloc.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include <stdlib.h>

static GSList* connections = 0;


dsmesock_connection_t* dsmesock_connect(void)
{
  dsmesock_connection_t* ret               = 0;
  int                    fd;
  struct sockaddr_un     c_addr;
  const char*            dsmesock_filename = NULL;

  dsmesock_filename = getenv("DSME_SOCKFILE");
  if (!dsmesock_filename) {
      dsmesock_filename = "/tmp/dsmesock";
  }

  if ((fd = socket(PF_UNIX, SOCK_STREAM, 0)) != -1) {

      memset(&c_addr, 0, sizeof(c_addr));
      c_addr.sun_family = AF_UNIX;
      strcpy(c_addr.sun_path, dsmesock_filename);

      if (connect(fd, (struct sockaddr *)&c_addr, sizeof(c_addr)) == -1 ||
          (ret = dsmesock_init(fd)) == 0)
      {
        close(fd);
        fd = -1;
      }

  }

  if (fd != -1) {
    struct linger linger;

    linger.l_onoff  = 1;
    linger.l_linger = 2;

    setsockopt(fd, SOL_SOCKET, SO_LINGER, &linger, sizeof(linger));
  }

  return ret;
}


dsmesock_connection_t* dsmesock_init(int fd)
{
  dsmesock_connection_t* newconn;

  if (fd == -1) return 0;

  if(-1 == fcntl(fd, F_SETFL, O_NONBLOCK))  return 0;

  newconn = (dsmesock_connection_t*)malloc(sizeof(dsmesock_connection_t));
  if (newconn == 0) return 0;

  memset(newconn, 0, sizeof(dsmesock_connection_t));
  newconn->fd      = fd;
  newconn->is_open = 1;
  newconn->channel = 0;

  connections = g_slist_prepend(connections, newconn);

  return newconn;
}


void* dsmesock_receive(dsmesock_connection_t* conn)
{
#define DSMESOCK_BUF_SIZE_DEFAULT  1024
#define DSMESOCK_BUF_SIZE_MAX     65536
  socklen_t          optlen;
  ssize_t            ret = 1;
  int                read_size;
  DSM_MSGTYPE_CLOSE* ret_close;
  unsigned           close_reason;
  void*              result;
  GSList*            node;

  /* Is this connection valid? */
  node = g_slist_find(connections, conn);
  if (node == 0 || conn->is_open == 0) {
      close_reason = TSMSG_CLOSE_REASON_ERR;
      goto return_close_reason;
  }

  optlen = sizeof(conn->ucred);
  if(getsockopt(conn->fd, SOL_SOCKET, SO_PEERCRED,
                &conn->ucred, &optlen) == -1)
  {
      /* that fails, fill some bogus values */
      conn->ucred.pid = 0;
      conn->ucred.uid = -1;
      conn->ucred.gid = -1;
  }

  /* Allocate buffer if necessary */
  if (conn->bufsize == 0 || conn->buf == 0) {
      /* Begin with 1k buffer (more than enough for most purposes) */
      conn->buf = (unsigned char*)malloc(DSMESOCK_BUF_SIZE_DEFAULT);
      if (conn->buf == 0) return 0;
      conn->bufused = 0;
      conn->bufsize = DSMESOCK_BUF_SIZE_DEFAULT;
  }

  /* read message header */
  while (conn->bufused < sizeof(dsmemsg_generic_t)) {
      read_size = sizeof(dsmemsg_generic_t) - conn->bufused;

      if ((ret = read(conn->fd, conn->buf+conn->bufused, read_size)) <= 0) {
          break;
      }

      conn->bufused += ret;
  }

  /* read message body (if any) */
  if (conn->bufused >= sizeof(dsmemsg_generic_t)) {
      dsmemsg_generic_t* msg = (dsmemsg_generic_t*)conn->buf;

      if (msg->line_size_ <= DSMESOCK_BUF_SIZE_MAX) {
          /* increase buffer if necessary */
          if (conn->bufsize < msg->line_size_) {
              unsigned char* newbuf = (unsigned char*)realloc(conn->buf,
                                                              msg->line_size_);
              if (newbuf == 0) return 0; /* Try again later */
              conn->buf     = newbuf;
              conn->bufsize = msg->line_size_;
          }

          while (conn->bufused < msg->line_size_) {
              read_size = msg->line_size_ - conn->bufused;

              if ((ret = read(conn->fd, conn->buf+conn->bufused, read_size)) <=
                  0)
                {
                  break;
                }

              conn->bufused += ret;
          }
      }
  }

  /* deal with errors */
  if (ret == 0) {
      /* Connection closed by remote */
      close_reason = TSMSG_CLOSE_REASON_EOF;
      goto discard_and_return_close_reason;
  } else if (ret < 0) {
      /* TODO: IS IT OK TO LEAVE RETRY TO THE CALLER? */
      if (errno == EWOULDBLOCK) return 0; /* Ok, no data available */
      if (errno == EINTR) return 0;       /* Got signal. retry (later) */

      /* Error encountered. Free up resources and report close. */
      close_reason = TSMSG_CLOSE_REASON_ERR;
      goto discard_and_return_close_reason;
  } else if (conn->bufused < sizeof(dsmemsg_generic_t) ||
             ((dsmemsg_generic_t*)conn->buf)->line_size_ >
             DSMESOCK_BUF_SIZE_MAX)
    {
      /* too short or long message; assume out-of-sync situation */
      close_reason = TSMSG_CLOSE_REASON_OOS;
      goto discard_and_return_close_reason;
    }

  /* success; detach the buffer from connection context and return it */
  result        = conn->buf;
  conn->buf     = 0;
  conn->bufsize = 0;
  conn->bufused = 0;
  return result;

  /* error cases */
discard_and_return_close_reason:
  conn->is_open = 0;
  free(conn->buf);
  conn->buf     = 0;
  close(conn->fd);
  conn->fd      = -1;
return_close_reason:
  ret_close = DSME_MSG_NEW(DSM_MSGTYPE_CLOSE);
  ret_close->reason = close_reason;
  return ret_close;
}


void dsmesock_close(dsmesock_connection_t* conn)
{
  GSList* node;

  node = g_slist_find(connections, conn);
  if (node != 0) {
      if (conn->buf != 0) free(conn->buf);
      if (conn->fd != -1) close(conn->fd);
      free(conn);
      connections = g_slist_delete_link(connections, node);
      return;
  }
}


int dsmesock_send(dsmesock_connection_t* conn, const void* msg)
{
  return dsmesock_send_with_extra(conn, msg, 0, 0);
}

int dsmesock_send_with_extra(dsmesock_connection_t* conn,
                             const void*            msg,
                             size_t                 extra_size,
                             const void*            extra)
{
  GSList*                  node;
  const dsmemsg_generic_t* m = (dsmemsg_generic_t*)msg;
  dsmemsg_generic_t        header;
  struct iovec             buffers[3];
  int                      count = 0;

  /* Is this connection valid? */
  node = g_slist_find(connections, conn);
  if (node == 0 || conn->is_open == 0) {
    return -1;
  }

  /* set up message header for sending */
  memcpy(&header, msg, sizeof(header));
  header.line_size_ += extra_size;
  buffers[count].iov_base = &header;
  buffers[count].iov_len  = sizeof(header);
  ++count;

  /* set up message body (if any) for sending */
  if (m->line_size_ > sizeof(header)) {
    buffers[count].iov_base = (void*)(((const char*)msg) + sizeof(header));
    buffers[count].iov_len  = m->line_size_ - sizeof(header);
    ++count;
  }

  /* set up extra part of the message (if any) for sending */
  if (extra_size > 0) {
    buffers[count].iov_base = (void*)extra;
    buffers[count].iov_len  = extra_size;
    ++count;
  }

  /* send the message */
  return writev(conn->fd, buffers, count);
}


void dsmesock_broadcast(const void* msg)
{
  dsmesock_broadcast_with_extra(msg, 0, 0);
}

void dsmesock_broadcast_with_extra(const void* msg,
                                   size_t      extra_size,
                                   const void* extra)
{
  GSList* node;

  for (node = connections; node != 0; node = g_slist_next(node)) {
      dsmesock_send_with_extra((dsmesock_connection_t *)(node->data),
                               msg,
                               extra_size,
                               extra);
  }
}

const struct ucred* dsmesock_getucred(dsmesock_connection_t* conn)
{
    GSList* node;

    node = g_slist_find(connections, conn);
    if (node != 0) {
        return &conn->ucred;
    }

    return 0;
}
