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

/* TODO: submit bug report to bme: */
/* bme header files are not self-standing; do the necessary typedefs */
#include <sys/types.h>
typedef __uint8_t  uint8;
typedef __uint16_t uint16;
typedef __uint32_t uint32;
typedef uint8      byte;
typedef __int16_t  int16;
typedef __int32_t  int32;

/* TODO: submit bug report to bme: */
/* these need to be #defined in order to pick up thermal stuff from bme */
#define TESTSERVER

#include <bme/bme_extmsg.h>
#include <bme/client_ipc.h>
#include <bme/em_isi.h> 


bool dsme_get_battery_temperature(int* temperature)
{
  bool got_temperature = false;

  /* read battery temperature from BME over IPC */

  if (bme_connect() >= 0) {
      int                     res;
      int                     nb = 0;
      union emsg_battery_info msg;

      msg.request.type    = EM_BATTERY_INFO_REQ;
      msg.request.subtype = 0;
      msg.request.flags   = EM_BATTERY_TEMP;
      res = bme_send_get_reply(&msg, sizeof(msg.request),
                               &msg, sizeof(msg.reply),
                               &nb);
      if (res >= 0 && nb == sizeof(msg.reply)) {
          *temperature = msg.reply.temp;
          got_temperature = true;
      } else {
          dsme_log(LOG_ERR, "Cannot get BTEMP (res=%d, nb=%d)", res, nb);
      }

      bme_disconnect();
  }

  return got_temperature;
}
