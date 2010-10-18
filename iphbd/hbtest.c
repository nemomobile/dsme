/**
   @brief Test utility for IP Heartbeat service

   @file hbtest.c

   This is the test utility for IP Heartbeat service.
   It tries to emulate applications that use IP (UDP or TCP) and
   go to sleep to wait for timeout to send "keepalive" message or some input.


   This test pgm creates a TCP connection to a remote peer and runs in the following loop:

   @code
   while (1) {
     send 4 segments of 128 bytes to the server within 40 msecs
     sleep pseudorandom 30-90 msecs (to get randomness to TCP keepalive scheduling)
     send 1 segment of 128 bytes to the server

     wait IP heartbeat for 60 secs, or data from test server 
	(in this test the server never sends)
   }
   @endcode

   See tests/TEST script for more information.


   
   <p>
   Copyright (C) 2008-2010 Nokia Corporation.

   @author Raimo Vuonnala <raimo.vuonnala@nokia.com>

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



#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ioctl.h>
/* socket transport */
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>

#include "libiphb.h"

static volatile int run = 1;

#define ME "hbtest: "


#define SLEEP_TIME    60

static int debugmode = 0;

static void 
sig_handler(int signo)
{
  switch (signo) {
  case SIGQUIT:
  case SIGTERM:
  case SIGINT:
    run = 0;
    break;
  default:
    fprintf(stderr, ME "\aERROR, unknown signal %d\n", signo);
  }
}


#define BUFLEN 128

int 
main (int argc, char *argv[])
{
  iphb_t 	      hb = 0;
  int    	      sock = -1;
  struct sockaddr_in  addr;
  char                buf_data;
  int                 hbsock;
  unsigned long       packets_sent = 0;
  int                 randomsleep = 0;
  int                 first = 1;
  

  if (argc < 4) {
    printf("Usage: %s IPAddress port TCP_keepalive_period [-d]\n", argv[0]);
    exit(1);
  }
  if (argc >= 5 && strcmp(argv[4], "-d") == 0)
    debugmode = 1;
  

  signal(SIGINT, sig_handler);
  signal(SIGTERM, sig_handler);
  signal(SIGPIPE, SIG_IGN);


  /* We open TCP socket where we send data to emulate real life app */
  sock = socket(PF_INET, SOCK_STREAM, 0);
  if (sock == -1) {
    perror(ME "socket()");
    run = 0;
  }
  else {
    int optval;

    memset(&addr, 0, (size_t)sizeof(struct sockaddr_in));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(atoi(argv[2]));
    if (inet_aton(argv[1], &addr.sin_addr) == 0) {
        fprintf(stderr, ME "\nERROR invalid address '%s'\n", argv[1]);
        goto close_sock_and_exit;
    }

    if (debugmode) printf(ME "connecting to %s:%d, TCP keepalive period is %d\n", 
			  inet_ntoa(addr.sin_addr), 
			  (int)ntohs(addr.sin_port), 
			  atoi(argv[3]));

    if (connect(sock, (struct sockaddr *)&addr, sizeof(struct sockaddr_in)) < 0) {
      perror(ME "connect()");
      run = 0;
    }
    else {

      optval = 1;
      setsockopt(sock, 
		 IPPROTO_TCP,
		 TCP_NODELAY,
		 (char *)&optval, sizeof(int));


      optval = 1;
      setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (char *)&optval, sizeof(int));


      /* grace period before sending first keepalive */
      optval = atoi(argv[3]);

      if (optval > 0) {

	printf(ME "setting TCP keepalive timers to %d secs\n", optval);

	setsockopt(sock, SOL_TCP, TCP_KEEPIDLE, (char *)&optval, sizeof(int));

	/* we can loose this many keepalives before connection is reset */
	optval = 10;
	setsockopt(sock, SOL_TCP, TCP_KEEPCNT, (char *)&optval, sizeof(int));

	/* time between keepalives */
	optval = atoi(argv[3]);
	setsockopt(sock, SOL_TCP, TCP_KEEPINTVL, (char *)&optval, sizeof(int));
      }
    }
  }

  if (run) {
    printf(ME "running\n");
    if (!hb) {
      hb = iphb_open(0);
      if (!hb)
	perror(ME "\aERROR, iphb_open()");
      else {
	struct iphb_stats stats;

	printf(ME "iphb service opened\n");

	if (iphb_get_stats(hb, &stats) == -1) 
	  fprintf(stderr, ME "\aERROR, iphb_get_stats() failed %s\n", strerror(errno));
	else
	  printf(ME "iphb_get_stats(): clients=%u, waiting=%u, next hb=%u secs\n", stats.clients, stats.waiting, stats.next_hb);


      }
    }
  }


  buf_data = '!' + getpid() % (126 - '!');

  srand(time(0));

  hbsock = iphb_get_fd(hb);


  /*  Generate data, sleep */
  for (;run;) {
    fd_set 	      	readfds;
    struct timeval    	timeout;
    int               	st, i;
    time_t            	now;
    time_t              then;
    time_t              went_to_sleep;
    char 		buf[BUFLEN];


    if (debugmode) printf(ME "sending data to server and sleep %d msecs...\n", 
			  randomsleep*30);

    for (i = 0; i < BUFLEN; i++)
      buf[i] = buf_data;


    for (i = 0; i < 4; i++) {
      usleep(10*1000);  /* sleep 10 millisecs */
      if (send(sock, buf, BUFLEN, 0) < 0) {
	perror(ME "send()");
	run = 0;
	break;
      }
      packets_sent++;
    }

    /* sleep 30 - 90 msecs secs to get randomness to TCP keep-alives 
       (not in the first round, though) */
    if (!randomsleep)
      randomsleep = (rand() % 3) + 1;  /* get random value 1..3 */
    else {
      randomsleep++;
      if (randomsleep > 3)
	randomsleep = 1;
   
      usleep(randomsleep*30*1000);  /* 30-90 msecs */

      randomsleep = (rand() % 3) + 1;  /* get random value 1..3 */

    }
    if (send(sock, buf, BUFLEN, 0) < 0) {
      perror(ME "send()");
      run = 0;
      continue;
    }
    packets_sent++;
    



    then = went_to_sleep = time(0);


    /* indicate iphbd that I want a wakeup */
    if (hb) {
        if (iphb_wait(hb, first ? 0 : SLEEP_TIME - 15, SLEEP_TIME, 0) != 0) {
	perror(ME "\aERROR, iphb_wait()");
	hb = iphb_close(hb);
      }
      first = 0;
    }

    if (debugmode) printf(ME "waiting for iphbd wakeup or server data...\n");

    timeout.tv_sec = SLEEP_TIME;
    timeout.tv_usec = 0;

    /* Wait events from "server" and iphbd */
    FD_ZERO(&readfds);
    if (hbsock != -1)
      FD_SET(hbsock, &readfds);
    FD_SET(sock, &readfds);


    st = select(sock > hbsock ? sock + 1 : hbsock + 1, &readfds, NULL, NULL, &timeout);

    if (hb) {
        int st;
        if (iphb_I_woke_up(hb) == -1)
            fprintf(stderr, ME "\aERROR, iphb_I_woke_up() %s\n", strerror(errno));
        st = iphb_discard_wakeups(hb);
        if (st == -1)
            fprintf(stderr, ME "\aERROR, iphb_discard_wakeups() %s\n", strerror(errno));
        else
            if (debugmode) printf(ME "discarded %d bytes\n", st);

    }

    now = time(0);
    if (st == -1) {
      if (errno == EINTR)
	continue;  
      else {
	perror(ME "\aERROR, select()");
	run = 0;
      }
    }
    else
    if (st >= 0) {
      if (now - then > SLEEP_TIME + 1)  /* allow 1 sec slippage */
	fprintf(stderr, ME "\aERROR, select() did not fire as expected, took %d secs\n", 
		(int)(now - then));

      if (debugmode) printf(ME "slept %d secs\n", (int)(now - then));

      if (hbsock != -1 && FD_ISSET(hbsock, &readfds)) {
	if (debugmode) printf(ME "select() woken by iphbd, waited %d secs\n", 
			      (int)(now - then));
      }

      if (FD_ISSET(sock, &readfds)) {
	if (debugmode) printf(ME "got data from server\n");
	if (recv(sock, buf, sizeof(buf), 0) < 0) {
	  perror(ME "\aERROR, recv()");
	  run = 0;
	  continue;
	}


      }

      if (debugmode) 
	printf(ME "has to wake up, cause time since last keep-alive happened %d secs ago\n", 
	       (int)(now - went_to_sleep));
    }
  }  
  printf(ME "bye, sent %lu packets!\n", packets_sent);

  if (hb)
    iphb_close(hb);

close_sock_and_exit:
  if (sock != -1)
    close(sock);
  return 0;
}

