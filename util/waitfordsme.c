/**
   @file waitfordsme.c

   This program blocks until the DSME socket is ready.
   Timeout is also defined.
   <p>
   Copyright (C) 2004-2011 Nokia Corporation.

   @author Ismo Laitinen
   @author Matias Muhonen <ext-matias.muhonen@nokia.com>

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

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/time.h>

#define DSME_START_TIMEOUT 5

static bool send_to_dsme(dsmesock_connection_t* conn, const void* msg)
{
	bool success = true;
	if (dsmesock_send(conn, msg) == -1) {
		success = false;
	}
	return success;
}

int main(int argc, char* argv[]) {
	
	struct timeval start;
	struct timeval now;
	dsmesock_connection_t *conn;
	DSM_MSGTYPE_GET_VERSION req_msg = DSME_MSG_INIT(DSM_MSGTYPE_GET_VERSION);

	gettimeofday(&start, NULL);

	printf("%s (pid %i): Wait for DSME socket...\n", argv[0], getpid());
	fflush(stdout);

	while (1) {
		conn = dsmesock_connect();
		if (conn > 0) {
			/* Give DSME time to accept the client socket before closing
			   the socket connection */
			if (!send_to_dsme(conn, &req_msg)) {
				dsmesock_close(conn);
				printf("%s: ERROR: send to DSME socket failed\n", argv[0]);
				return EXIT_FAILURE;
			} else {
				dsmesock_close(conn);
				printf("%s: OK: send to DSME socket succeeded\n", argv[0]);
				return EXIT_SUCCESS;
			}
		}
		
		gettimeofday(&now, NULL);
		
		if (now.tv_sec >= (start.tv_sec + DSME_START_TIMEOUT)) {
			fprintf(stderr,
				    "%s: ERROR: Timeout waiting for DSME socket\n",
				    argv[0]);
			return EXIT_FAILURE;
		}
		usleep(20000);
	}

	return EXIT_FAILURE;
}
