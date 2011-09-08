/**
   @file stub_cal.h

   A simple test driver and test cases for DSME
   <p>
   Copyright (C) 2011 Nokia Corporation

   @author Jyrki Hämäläinen <ext-jyrki.hamalainen@nokia.com>

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


#ifndef DSME_TEST_STUB_CAL_H
#define DSME_TEST_STUB_CAL_H

struct cal;

int cal_read_block(struct cal*    cal,
                   const char*    name,
                   void**         ptr,
                   unsigned long* len,
                   unsigned long  flags);

#define DEFAULT_RD_MODE "1"
static const char* rd_mode = DEFAULT_RD_MODE;

int cal_read_block(struct cal*    cal,
                   const char*    name,
                   void**         ptr,
                   unsigned long* len,
                   unsigned long  flags)
{
  int result = -1;

  if (strcmp(name, "r&d_mode") == 0) {
      if (rd_mode) {
          *ptr = malloc(2);
          strcpy(*ptr, rd_mode);
          *len = strlen(rd_mode);
          result = 0;
      }
  } else {
      fatal("cal_read_block(\"%s\")", name);
  }

  return result;
}

#endif /* DSME_TEST_STUB_CAL_H */
