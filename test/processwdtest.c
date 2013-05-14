/**
   @file processwdtest.c

   This is a test for processwd module.
   <p>
   Copyright (C) 2004-2010 Nokia Corporation.

   @author Ari Saastamoinen

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

#include <dsme/state.h>
#include <dsme/messages.h>
#include <dsme/protocol.h>
#include <dsme/processwd.h>

#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>

#include <stdio.h>
#define tblog(a, b, c) fprintf(stderr, c);

#ifndef DSME
#define DSME "/foo/bar/dsme"
#endif

#define CMDLINE DSME " -p libswwd.so"


#ifdef STARTDSME
static pid_t execdsme(void);
static pid_t execdsme(void)
{
  pid_t pid;

  pid = fork();
  if (pid == -1) { /* Error */
    return 0;
  } else if (pid == 0) {  /* child */
    execl("/bin/sh", "sh", "-c", CMDLINE, NULL);
    exit(EXIT_FAILURE);  /* Exec shoud not return - just in case exit */
  }
  return pid;
}
#endif


int dsmeprocesswdtest(int);
int dsmeprocesswdtest(int testnum)
{
	dsmesock_connection_t * conn = 0;
	int retval = -1;
	int i = -1;
	pid_t child = 0;
	time_t curtime;
	int status;

#ifdef STARTDSME
	pid_t pid = 0;
	pid = execdsme();
	if (!pid) {
		tblog(testnum, TBLOG_ERROR, "Cannot fork\n");
		return -1;
	}
#endif

	 /* Try 5 secs to connect */
  for (i = 0 ; i < 5 ; i++) {
    sleep(1);
    conn = dsmesock_connect();
    if (conn) {
      break;
    }
  }
	
	if (!conn) {
    tblog(testnum, TBLOG_ERROR, "Cannot connect to DSME\n");
    goto cleanup;
  }

	child = fork();
	if (child < 0) {
		tblog(testnum, TBLOG_ERROR, "Cannot fork\n");
		goto cleanup;
	} else if (child == 0) {
		/* Child */
                DSM_MSGTYPE_PROCESSWD_CREATE msg =
                  DSME_MSG_INIT(DSM_MSGTYPE_PROCESSWD_CREATE);
		int phase = 0;

		msg.pid = getpid();

		if (dsmesock_send(conn, &msg) < 0) {
			tblog(testnum, TBLOG_ERROR, "Cannot send to DSME\n");
			exit(1);
		}

		tblog(testnum, TBLOG_INFO, "Wait for PING message\n");

		curtime = time(NULL);
		while (time(NULL) < curtime + 150) {
			dsmemsg_generic_t * inmsg;
			
			inmsg = dsmesock_receive(conn);
			if (inmsg) {
				if (phase < 3) {
					if (DSMEMSG_CAST(DSM_MSGTYPE_PROCESSWD_PING, inmsg)) {
                                                DSM_MSGTYPE_PROCESSWD_PONG outmsg =
                                                  DSME_MSG_INIT(DSM_MSGTYPE_PROCESSWD_PONG);
                                                outmsg.pid = getpid();
						tblog(testnum, TBLOG_INFO, "Got ping - send pong\n");
						dsmesock_send(conn, &outmsg);
					}
					phase++;
				}else{
					tblog(testnum, TBLOG_INFO, "Got ping - do not send pongs anymore\n");
				}
				free(inmsg);
			}
		}
		tblog(testnum, TBLOG_ERROR, "Still alive - exiting\n");
		exit(1);
	}

	/* Main process */
	curtime = time(NULL);
	while(waitpid(child, &status, 0) != child) ;
	
	if (time(NULL) < curtime + 27) {
		tblog(testnum, TBLOG_ERROR, "Killed too early\n");
		goto cleanup;
	}
	
	if (time(NULL) > curtime + 150) {
		tblog(testnum, TBLOG_ERROR, "Didn't get killed\n");
		goto cleanup;
	}

	retval = 0;
		
 cleanup:
	if (conn) {
		dsmesock_close(conn);
	}

#ifdef STARTDSME
	if (pid) {
		/* KILL DSME */
		kill(pid, SIGKILL);
		waitpid(pid, NULL, 0);
	}
#endif

	return retval;
}

int main(void)
{
  dsmeprocesswdtest(0);
  return 0;
}
