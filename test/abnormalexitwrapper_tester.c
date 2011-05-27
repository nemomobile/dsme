/**
   @file abnormalexitwrapper_tester.c

   A simple test driver and test cases for DSME
   <p>
   Copyright (C) 2011 Nokia Corporation

   @author Jyrki Hämäläinen <ext-jyrki.pe.hamalainen@nokia.com>

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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>

int waiting = 1;

void usr1_handler(int signum);

void usr1_handler(int signum)
{
  waiting = 0;
}


int main()
{
  printf("\nForkwrapper tester\n");

  signal(SIGUSR1, usr1_handler);

  while(waiting) {
      sleep(1);
  }

  pid_t mypid = fork();

  if (mypid == -1) {
      printf("\nForking failed\n");
  } else if (mypid == 0) {
      printf("\nForking succeeded\n");
  } else {
  }

  exit(EXIT_SUCCESS);
}
