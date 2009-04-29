/**
   @file thermalsensor_battery.c

   This module provides battery temperature readings.
   <p>
   Copyright (C) 2009 Nokia Corporation

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

#include <bme/client_ipc.h>
#include <bme/bme_extmsg.h>
#include <bme/em_isi.h> 


static gboolean handle_battery_temperature_response(GIOChannel*  source,
                                                    GIOCondition condition,
                                                    gpointer     data);
static gboolean read_status(void);
static gboolean read_temperature(void);


void*       the_cookie;
void      (*report_temperature)(void* cookie, int temperature);

static int  request_fd = -1;
static bool got_status = false;

static void connect_bme()
{
  got_status = false;

  if ((request_fd = bme_connect()) == -1) {
      dsme_log(LOG_DEBUG, "could not connect to bme");
  } else {
      dsme_log(LOG_DEBUG, "connected to bme");

      GIOChannel* chan;

      if (!(chan = g_io_channel_unix_new(request_fd)) ||
          !g_io_add_watch(chan,
                          (G_IO_IN | G_IO_ERR | G_IO_HUP),
                          handle_battery_temperature_response,
                          0))
      {
          g_io_channel_unref(chan);
          bme_disconnect();
          request_fd = -1;
          dsme_log(LOG_ERR, "g_io error; disconnected from bme");
      }
  }
}

static void disconnect_bme()
{
  if (request_fd != -1) {
      bme_disconnect();
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
      if (bme_write(&req, sizeof(req)) != sizeof(req)) {
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
  if (condition & (G_IO_ERR | G_IO_HUP)) {
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
  int  err             = -1;

  dsme_log(LOG_DEBUG, "read status from bme");
  if (bme_read(&err, sizeof(err)) == sizeof(err) && !err) {
      keep_connection = true;
  }

  return keep_connection;
}

static gboolean read_temperature(void)
{
  bool                           keep_connection = false;
  struct emsg_battery_info_reply resp;

  dsme_log(LOG_DEBUG, "read temperature from bme");
  if (bme_read(&resp, sizeof(resp)) == sizeof(resp)) {
      keep_connection = true;

      /* report the temperature to the thermal manager */
      report_temperature(the_cookie, resp.temp);
  }

  return keep_connection;
}
