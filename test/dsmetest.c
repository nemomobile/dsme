/**
   @file dsmetool.c

   Dsmetool can be used to send commands to DSME.
   <p>
   Copyright (C) 2004-2010 Nokia Corporation.

   @author Ismo Laitinen <ismo.laitinen@nokia.com>
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

#include <dsme/protocol.h>
#include <dsme/messages.h>
#include <dsme/state.h>
#include "../modules/malf.h"

#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

void usage(const char *name);
void send_shutdown_req(bool battlow);
void send_powerup_req(void);
void send_reboot_req(void);
void send_alarm_state(bool alarm_set);
void send_malf_req(void);

static dsmesock_connection_t *conn;

void usage(const char *name)
{
	printf("USAGE: %s <options>\n", name);
	printf("THESE ARE JUST FOR DEBUGGING\n");
	printf("	-U --powerup			Request powerup from the ActDead from DSME\n");
	printf("	-s --shutdown			Request normal shutdown from DSME\n");
	printf("	-B --battlow			Request battery low shutdown from DSME\n");
	printf("	-R --reboot			Request reboot from DSME\n");
	printf("	-a --alarm			Change alarm state (0 = not set, 1 = set)\n");
	printf("	-M --malf			Request MALF state\n");
        printf("	-h --help			Print usage\n");

}

void send_shutdown_req(bool battlow)
{
  if (battlow) {
      DSM_MSGTYPE_SET_BATTERY_STATE msg =
          DSME_MSG_INIT(DSM_MSGTYPE_SET_BATTERY_STATE);

      msg.empty = true;
      dsmesock_send(conn, &msg);
      printf("battery empty sent!\n");
  } else {
      DSM_MSGTYPE_SHUTDOWN_REQ msg = DSME_MSG_INIT(DSM_MSGTYPE_SHUTDOWN_REQ);
      dsmesock_send(conn, &msg);
      printf("shutdown request sent!\n");
  }
}

void send_alarm_state(bool alarm_set)
{
	DSM_MSGTYPE_SET_ALARM_STATE msg =
          DSME_MSG_INIT(DSM_MSGTYPE_SET_ALARM_STATE);

	msg.alarm_set = alarm_set;

	dsmesock_send(conn, &msg);
	printf("alarm state sent!\n");
}

void send_powerup_req(void)
{
  DSM_MSGTYPE_POWERUP_REQ msg = DSME_MSG_INIT(DSM_MSGTYPE_POWERUP_REQ);
  dsmesock_send(conn, &msg);
  printf("Powerup request sent!\n");
}

void send_reboot_req(void)
{
  DSM_MSGTYPE_REBOOT_REQ msg = DSME_MSG_INIT(DSM_MSGTYPE_REBOOT_REQ);
  dsmesock_send(conn, &msg);
  printf("Reboot request sent!\n");
}

void send_malf_req(void)
{
  char* details = strdup("Entering malf from dsmetest");
  DSM_MSGTYPE_ENTER_MALF msg = DSME_MSG_INIT(DSM_MSGTYPE_ENTER_MALF);
  msg.reason = DSME_MALF_SOFTWARE;
  msg.component = NULL;
  
  dsmesock_send_with_extra(conn, &msg, sizeof(details), details);
  printf("MALF request sent!\n");
}

int main(int argc, char *argv[])
{
	const char *program_name = argv[0];
	int next_option;
	int retval = 0;
	int new_state = -1;
	int powerup = -1;
	int reboot = -1;
	int alarm_tmp = -1;
        int malf = -1;
	const char *short_options = "UBshRMa:";
	const struct option long_options[] = {
		{"shutdown", 0, NULL, 's'},
		{"battlow", 0, NULL, 'B'},
		{"alarm", 1, NULL, 'a'},
		{"help", 0, NULL, 'h'},
		{"powerup", 0, NULL, 'U'},
		{"reboot", 0, NULL, 'R'},
        	{"malf", 0, NULL, 'M'},
		{0, 0, 0, 0}
	};

	do {
		next_option =
		    getopt_long(argc, argv, short_options, long_options,
				NULL);
		switch (next_option) {
		case 's':
			new_state = 0;
			break;
		case 'U':
			powerup = 1;
			break;
		case 'R':
			reboot = 1;
			break;
		case 'B':
			new_state = 1;
			break;
		case 'a':
			alarm_tmp = atoi(optarg);
			break;
		case 'M':
			malf = 1;
                        break;
		case 'h':
			usage(program_name);
			return EXIT_SUCCESS;
			break;
		break;
		case '?':
			usage(program_name);
			return EXIT_FAILURE;
			break;
		}
	} while (next_option != -1);

	/* check if unknown parameters or no parameters at all were given */
	if (argc == 1 || optind < argc) {
		usage(program_name);
		return EXIT_FAILURE;
	}

	conn = dsmesock_connect();
	if (conn == 0) {
		perror("dsmesock_connect");
		return EXIT_FAILURE;
	}

	if (powerup != -1)
		send_powerup_req();

	if (reboot != -1)
		send_reboot_req();

	if (new_state != -1)
		send_shutdown_req(new_state);

	if (alarm_tmp != -1)
		send_alarm_state(alarm_tmp != 0);

        if (malf != -1)
                send_malf_req();

	dsmesock_close(conn);

	return retval;
}
