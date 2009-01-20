/**
   @file thermal-tool.c

   Util for thermal management, e.g. for obtaining values from CAL
   <p>
   Copyright (C) 2004-2009 Nokia Corporation.

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

#include <cal.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

static bool print_thermal_cal_flags(struct cal* cal)
{
  bool          done = false;
  void*         p;
  unsigned long len;

  /* TODO: use the right block name when it becomes available */
  if (cal_read_block(cal, "r&d_mode", &p, &len, CAL_FLAG_USER) == 0 && len > 1) {
      printf("%s", (char*)p);
      done = true;
  }

  return done;
}

int main(int argc, char* argv[])
{
  int         status = EXIT_FAILURE;
  struct cal* cal;

  if (cal_init(&cal) < 0) {
      fprintf(stderr, "CAL initialization failed\n");
  } else {

      if (print_thermal_cal_flags(cal)) {
          status = EXIT_SUCCESS;
      }

      cal_finish(cal);
  }

  return status;
}
