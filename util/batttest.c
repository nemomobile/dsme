/**
   @file battest.c

   This is just a test for battery messages..
   <p>
   Copyright (C) 2004-2009 Nokia Corporation.

   @author Ismo Laitinen

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

#include <dsme/protocol.h>
#include <dsme/state.h>

#include <getopt.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void usage(const char* name);

void usage(const char* name)
{
  printf("%s [-b <state>] [-c <state>]\n", name);
  printf("  -b  --battery           Send battery status (0=low,  1=ok)\n");
  printf("  -c  --charger           Send charger status (0=disc, 1=conn)\n");
  printf("  -h  --help              Print usage\n");
}

int main(int argc, char *argv[])
{
  dsmesock_connection_t * conn;

  int next_option;
  const char* program_name = argv[0];

  int batt_status = -1;
  int charger_status = -1;

  const char* short_options = "hb:c:";
  const struct option long_options[] = {
    { "battery",     0, NULL, 'b' },
    { "charger",     0, NULL, 'c' },
    { "help",        0, NULL, 'h' }
  };

  do {
    next_option = getopt_long(argc, argv, short_options, long_options, NULL);
    switch (next_option) {
    case 'b':
      batt_status = atoi(optarg);
      break;
    case 'c':
      charger_status = atoi(optarg);
      break;
    case 'h':
      usage(program_name);
      return EXIT_SUCCESS;
      break;
    case '?':
      usage(program_name);
      return EXIT_FAILURE;
      break;
    }
  } while (next_option != -1);

  if (batt_status == -1 && charger_status == -1) {
    usage(program_name);
    return EXIT_FAILURE;
  }

  conn = dsmesock_connect();
  if (conn == 0) {
    perror("dsmesock_connect");
    return 2;
  }

  if (batt_status != -1) {
      DSM_MSGTYPE_SET_BATTERY_STATE msg =
          DSME_MSG_INIT(DSM_MSGTYPE_SET_BATTERY_STATE);

      msg.empty = !batt_status;

      dsmesock_send(conn, &msg);
  }

  if (charger_status != -1) {
      DSM_MSGTYPE_SET_CHARGER_STATE msg =
          DSME_MSG_INIT(DSM_MSGTYPE_SET_CHARGER_STATE);

      msg.connected = !!charger_status;

      dsmesock_send(conn, &msg);
  }

  dsmesock_close(conn);

  exit(0);
}
