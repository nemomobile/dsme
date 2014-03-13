/**
   @file thermalsensor_battery.c

   This module provides battery temperature readings.
   <p>
   Copyright (C) 2009-2010 Nokia Corporation

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

#include "thermalsensor_battery.h"
#include "dsme/logging.h"
#include <glib.h>
#include <string.h>

/* TODO: submit bug report to bme: */
/* bme header files are not self-standing; do the necessary typedefs */
#include <stdint.h>
typedef uint8_t  uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef int16_t  int16;
typedef int32_t  int32;
typedef uint8_t  byte;

/* TODO: submit bug report to bme: */
/* these need to be #defined in order to pick up thermal stuff from bme */
#define TESTSERVER

#ifdef GOT_BMEIPC_HEADERS

#include <bme/bmeipc.h>
#include <bme/bmemsg.h>
#include <bme/em_isi.h>

#else /* GOT_BMEIPC_HEADERS */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#define BME_SRV_SOCK_PATH       "/tmp/.bmesrv"
#define BME_SRV_COOKIE          "BMentity"

#define EM_BATTERY_INFO_REQ                      0x06
#define EM_BATTERY_TEMP                          0x0004  /* -------------1-- */

struct emsg_battery_info_req {
    uint16      type, subtype;
    uint32      flags;
};

/* Battery info reply */
struct emsg_battery_info_reply {
    uint32      a;
    uint32      flags;
    uint16      c;
    uint16      d;
    uint16      temp;
    uint16      f;
    uint16      g;
    uint16      h;
    uint16      i;
    uint16      j;
    uint16      k;
    uint16      l;
};

union emsg_battery_info {
    struct emsg_battery_info_req   request;
    struct emsg_battery_info_reply reply;
};

static int bme_socket = -1;

static int32_t bme_read(void *msg, int32_t bytes);
static int32_t bme_write(const void *msg, int32_t bytes);
static void bme_disconnect(void);
static int32_t bme_connect(void);

int32_t bme_read(void *msg, int32_t bytes)
{
  if (bme_socket == -1)
  {
    return -1;
  }
  return recv(bme_socket, msg, bytes, 0);
}

int32_t bme_write(const void *msg, int32_t bytes)
{
  if (bme_socket == -1)
  {
   return -1;
  }
  return send(bme_socket, msg, bytes, 0);
}

void bme_disconnect(void)
{
  if (bme_socket >= 0)
    close(bme_socket);
  bme_socket = -1;
}

int32_t bme_connect(void)
{
  struct sockaddr_un sa;
  char ch;

  memset(&sa, 0, sizeof(sa));
  sa.sun_family = PF_UNIX;

  strcpy(sa.sun_path, BME_SRV_SOCK_PATH);
  if ((bme_socket = socket(PF_UNIX,SOCK_STREAM, 0)) < 0)
    return bme_socket;

  if (connect(bme_socket, (struct sockaddr *) &sa, sizeof(struct sockaddr_un)) < 0)
  {
    bme_disconnect();
    return bme_socket;
  }

  /* Send cookie */
  if (bme_write(BME_SRV_COOKIE, strlen(BME_SRV_COOKIE)) < strlen(BME_SRV_COOKIE))
  {
    bme_disconnect();
    return bme_socket;
  }

  if (bme_read(&ch, 1) < 1 || ch != '\n')
  {
    bme_disconnect();
    return bme_socket;
  }
  return bme_socket;
}
#endif /* GOT_BMEIPC_HEADERS */

static gboolean handle_battery_temperature_response(GIOChannel*  source,
                                                    GIOCondition condition,
                                                    gpointer     data);
static gboolean read_status(void);
static gboolean read_temperature(void);


void*       the_cookie;
void      (*report_temperature)(void* cookie, int temperature);

static int32_t  request_fd = -1;
static bool got_status = false;

static void connect_bme()
{
  got_status = false;

  if ((request_fd = bmeipc_open()) == -1) {
      dsme_log(LOG_DEBUG, "could not connect to bme");
  } else {
      dsme_log(LOG_DEBUG, "connected to bme");

      GIOChannel* chan;

      if (!(chan = g_io_channel_unix_new(request_fd)) ||
          !g_io_add_watch(chan,
                          G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL,
                          handle_battery_temperature_response,
                          0))
      {
          g_io_channel_unref(chan);
          bmeipc_close(request_fd);
          request_fd = -1;
          dsme_log(LOG_ERR, "g_io error; disconnected from bme");
      }
  }
}

static void disconnect_bme()
{
  if (request_fd != -1) {
      bmeipc_close(request_fd);
      request_fd = -1;
      dsme_log(LOG_DEBUG, "disconnected from bme");
  }

  /* use temperature -1 to indicate that the request failed */
  report_temperature(the_cookie, -1);
}

extern bool dsme_request_battery_temperature(
                void* cookie,
                void (callback)(void* cookie, int temperature))
{
  bool request_sent = false;

  the_cookie         = cookie;
  report_temperature = callback;

  if (request_fd == -1) {
      connect_bme();
  }

  if (request_fd != -1) {
      struct emsg_battery_info_req req;

      memset(&req, 0, sizeof(req));
      req.type    = EM_BATTERY_INFO_REQ;
      req.subtype = 0;
      req.flags   = EM_BATTERY_TEMP;

      dsme_log(LOG_DEBUG, "sending a request to bme");
      if (bmeipc_send(request_fd, &req, sizeof(req)) != sizeof(req)) {
          disconnect_bme();
      } else {
          request_sent = true;
      }
  }

  return request_sent;
}

static gboolean handle_battery_temperature_response(GIOChannel*  source,
                                                    GIOCondition condition,
                                                    gpointer     data)
{
  bool keep_connection = true;

  if (condition & G_IO_IN) {
      if (!got_status) {
          keep_connection = read_status();
          got_status = true;
      } else {
          keep_connection = read_temperature();
          got_status = false;
      }
  }
  if (condition & (G_IO_ERR | G_IO_HUP | G_IO_NVAL)) {
      dsme_log(LOG_DEBUG, "bme connection ERR or HUP");
      keep_connection = false;
  }

  if (!keep_connection) {
      disconnect_bme();
  }

  return keep_connection;
}

static gboolean read_status(void)
{
  bool keep_connection = false;
  int32_t  err             = -1;

  dsme_log(LOG_DEBUG, "read status from bme");
  if (bmeipc_recv(request_fd, &err, sizeof(err)) == sizeof(err) && err >= 0) {
      keep_connection = true;
  }

  return keep_connection;
}

static gboolean read_temperature(void)
{
  bool                           keep_connection = false;
  struct emsg_battery_info_reply resp;

  dsme_log(LOG_DEBUG, "read temperature from bme");
  if (bmeipc_recv(request_fd, &resp, sizeof(resp)) == sizeof(resp)) {
      keep_connection = true;

      /* report the temperature to the thermal manager */
      report_temperature(the_cookie, resp.temp);
  }

  return keep_connection;
}
