/**
   @file bootstate.c

   This program blocks until it receives the state from dsme
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
#include <dsme/protocol.h>
#include <dsme/messages.h>

#include <stdio.h>
#include <stdlib.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/time.h>

#define DSME_STATE_TIMEOUT 40

static int state = DSME_STATE_MALF;

int main(void)
{
  dsmesock_connection_t*        dsme_conn;
  fd_set                        rfds;
  DSM_MSGTYPE_STATE_QUERY       req_msg =
    DSME_MSG_INIT(DSM_MSGTYPE_STATE_QUERY);
  dsmemsg_generic_t*            msg;
  DSM_MSGTYPE_STATE_CHANGE_IND* msg2;
  int ret;

  dsme_conn = dsmesock_connect();
  if (dsme_conn == 0) {
      fprintf(stderr, "dsmesock_connect\n");
      return state;
  }

  /* Sending a query if the original message has already gone by */
  dsmesock_send(dsme_conn, &req_msg);

  while (1) {
      struct timeval tv;
      tv.tv_sec = DSME_STATE_TIMEOUT;
      tv.tv_usec = 0;
      FD_ZERO(&rfds);
      FD_SET(dsme_conn->fd, &rfds);

      ret = select(dsme_conn->fd + 1, &rfds, NULL, NULL, &tv); 
      if (ret == -1) {
          fprintf(stderr, "error in select()\n");
          printf("MALF");
          return EXIT_FAILURE;
      } 
      if (ret == 0) {
          fprintf(stderr, "Timeout!\n");
          printf("MALF");
          return EXIT_FAILURE;
      }

      msg = (dsmemsg_generic_t*)dsmesock_receive(dsme_conn);
      if ((msg2 = DSMEMSG_CAST(DSM_MSGTYPE_STATE_CHANGE_IND, msg)) != 0) {
          fprintf(stderr, "received state:%i\n", msg2->state);
          switch (msg2->state) {
            case DSME_STATE_ACTDEAD:
              printf("ACTDEAD");
              return EXIT_SUCCESS;
            case DSME_STATE_USER:
              printf("USER");
              return EXIT_SUCCESS;
            case DSME_STATE_TEST:
              printf("TEST");
              return EXIT_SUCCESS;
            case DSME_STATE_LOCAL:
              printf("LOCAL");
              return EXIT_SUCCESS;
            case DSME_STATE_MALF:
              printf("MALF");
              return EXIT_SUCCESS;
            case DSME_STATE_SHUTDOWN:
              printf("SHUTDOWN");
              return EXIT_SUCCESS;
            case DSME_STATE_BOOT:
              printf("BOOT");
              return EXIT_SUCCESS;
            default:
              fprintf(stderr, "unknown state: %d\n", msg2->state);
              break;
          }
        } else {
            fprintf(stderr, "The received message wasn't state change indication\n");
        }
      free(msg);
  }

  return EXIT_FAILURE;
}
