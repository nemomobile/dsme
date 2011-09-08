/**
   @file testdriver.h

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


#ifndef DSME_TESTDIVER_H
#define DSME_TESTDRIVER_H

#include "stub_timers.h"

typedef void (testcase)(void);

#define run(TC) run_(TC, #TC)
static void run_(testcase* test, const char* name)
{
  fprintf(stderr, "\n[ ******** STARTING TESTCASE '%s' ******** ]\n", name);
  reset_timers();
  test();
  fprintf(stderr, "\n[ ******** DONE TESTCASE '%s' ******** ]\n", name);
}

#endif /* DSME_TESTDRIVER_H */
