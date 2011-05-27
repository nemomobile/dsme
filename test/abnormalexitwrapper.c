/**
   @file abnormalexitwrapper.c

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

#include <unistd.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <dbus/dbus.h>
#include <string.h>

int failfork = 0;

void __attribute__ ((constructor)) my_init(void);
void usr2_handler(int signum);

void usr2_handler(int signum)
{
  if (signum == SIGUSR2) {
      fprintf(stderr, "\nUSR2 received\n");
      failfork = 1;
  }
}

void __attribute__ ((constructor)) my_init(void)
{
  fprintf(stderr, "\nInitializing fork wrapper\n");

  signal(SIGUSR2, usr2_handler);
}


pid_t fork(void)
{
  pid_t return_code = -1;

  fprintf(stderr, "\n!!!Wrapped fork!!!\n");

  if (failfork) {
      return_code = -1;
  } else {
      pid_t (*realfork)(void) = dlsym(RTLD_NEXT, "fork");
      if (dlerror()) {
         return_code = -1;
      } else {
         return_code = realfork();
      }
  }

  return return_code;
}

DBusConnection* dbus_connection_open_private(
                                             const char *address,
                                             DBusError  *error)
{
  DBusConnection* conn = NULL;

  if (failfork && (strcmp(address, "unix:abstract=/com/ubuntu/upstart") == 0)) {
      conn = NULL;
  } else {
      DBusConnection* (*realopen)(const char*, DBusError*) = dlsym(RTLD_NEXT, "dbus_connection_open_private");
      if (dlerror()) {
          conn = NULL;
      } else {
          conn = realopen(address, error);
      }
  }

  return conn;
}

