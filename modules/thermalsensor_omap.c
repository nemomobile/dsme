/**
   @file thermalsensor_omap.c

   This module provides OMAP temperature readings.
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

#define _GNU_SOURCE
#include "thermalsensor_omap.h"
#include "../include/dsme/logging.h"

#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

static bool scan_file(const char* filename,
                      const char* format,
                      int         expected_items,
                      ...)
{
  FILE*   fp;
  int     scanned_items;
  va_list ap;

  if ((fp = fopen(filename, "r")) == NULL) {
      dsme_log(LOG_ERR, "Could not open '%s': %s", filename, strerror(errno));
      return false;
  }

  va_start(ap, expected_items);
  scanned_items = vfscanf(fp, format, ap);
  va_end(ap);

  fclose(fp);

  return scanned_items == expected_items;
}


bool dsme_omap_get_temperature(int* temperature)
{
  return scan_file("/sys/class/hwmon/hwmon0/device/temp1_input",
                   "%d",
                   1,
                   temperature);
}

bool dsme_omap_is_blacklisted(void)
{
  static bool blacklisted = false;
  static bool checked     = false;

  if (!checked) {
      FILE* f = fopen("/proc/cpuinfo", "r");

      if (f) {
          char*  line = 0;
          size_t len  = 0;
          int    revision;

          while (getline(&line, &len, f) != -1) {
               if (sscanf(line, "CPU revision : %d", &revision) == 1) {
                   if (revision == 2) {
                       blacklisted = true;
                       dsme_log(LOG_NOTICE, "blacklisted OMAP thermal sensor");
                   }
                   break;
               }
          }

          checked = true;

          free(line);
          fclose(f);
      }
  }

  return blacklisted;
}
