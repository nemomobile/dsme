/**
   @file kick_wd.c

   This program sends a message to dsme to kick WDs.
   <p>
   Copyright (C) 2004-2010 Nokia Corporation.

   @author Ismo Laitinen <ismo.laitinen@nokia.com>

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
#include "../modules/hwwd.h"

#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <stdio.h>
#include <string.h>

void usage(const char *name);
int send_kick_wd(void);

static dsmesock_connection_t *conn;

void usage(const char *name)
{
	printf("USAGE: %s \n", name);
	printf("Run the program to tell DSME to kick WDs.\n");
	printf("	-h --help			Print usage\n");
}

int send_kick_wd(void) {

	int ret = 0;
	DSM_MSGTYPE_HWWD_KICK msg =
          DSME_MSG_INIT(DSM_MSGTYPE_HWWD_KICK);

	ret = dsmesock_send(conn, &msg);
	printf("Message sent to DSME!");

	return ret;
}

int main(int argc, char *argv[])
{
	const char *program_name = argv[0];
	if (argc > 1) {
		usage(program_name);
		return EXIT_FAILURE;
	}

	conn = dsmesock_connect();
	if (conn == 0) {
		perror("dsmesock_connect");
		return EXIT_FAILURE;
	}

	if (send_kick_wd() <= 0) {
		printf("sending failed!\n");
		return EXIT_FAILURE;
	}

	dsmesock_close(conn);

	return EXIT_SUCCESS;
}
