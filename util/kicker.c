/**
   @file kicker.c

   This program kicks watchdogs when it gets permission from DSME.
   <p>
   Copyright (C) 2004-2009 Nokia Corporation.

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

#include <stdio.h>
#include <stdlib.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/watchdog.h>
#include <errno.h>

#include <cal.h>

#include "../modules/hwwd.h"
#include "dsme/protocol.h"

#define OOM_ADJ_VALUE -17


static const char wd_file[] = "/dev/watchdog";
static const int wd_period  = 30;

static int wd_enabled = 1;
static int wd_fd = -1;

static struct cal *kicker_cal;


static int protect_from_oom(void)
{
      FILE *file = 0;
      int ret = -1;
      char filename[128];

      if (sprintf(filename, "/proc/%i/oom_adj", getpid()) < 0) {
              fprintf(stderr, "file: %s\n", filename);
              return -1;
      }

      file = fopen(filename, "w");
      if (!file) {
              fprintf(stderr, "Kicker: failed to open file: %s\n", filename);
              return -1;
      }

      ret = fprintf(file, "%i", OOM_ADJ_VALUE);
      fclose(file);

      if (ret < 0) {
              fprintf(stderr, "Kicker: failed to write to file: %s\n", filename);
              return -1;
      }

      return 0;
}

static int read_cal_config(void)
{
      void *vptr = NULL;
      unsigned long len = 0;
      int ret = 0;
      char *p;

      if (cal_init(&kicker_cal) < 0) {
              fprintf(stderr, "Kicker: cal_init() failed\n");
              return -1;
      }

      ret = cal_read_block(kicker_cal, "r&d_mode", &vptr, &len, CAL_FLAG_USER);
      if (ret < 0) {
              fprintf(stderr, "Kicker: error reading R&D mode flags, watchdog enabled\n");
              cal_finish(kicker_cal);
              return -1;
      }
      p = (char*)vptr;
      if (len >= 1 && *p) {
              fprintf(stderr, "R&D mode enabled\n");

              if (len > 1) {
                      if (strstr(p, "no-omap-wd")) {
                              wd_enabled = 0;
                              fprintf(stderr, "WD kicking disabled\n");
                      } else {
                              wd_enabled = 1;
                      }
              } else {
                      wd_enabled = 1;
                      fprintf(stderr, "No WD flags found, kicking enabled!\n");
              }
      }

      free(vptr);

      cal_finish(kicker_cal);

      return 0;
}

static int init_wd(void)
{
      int tmp;
      int ret = 0;

      read_cal_config();
      if (!wd_enabled)
              return -1;

      if (wd_enabled) {
              wd_fd = open(wd_file, O_RDWR);
              if (wd_fd == 0) {
                      fprintf(stderr, "Kicker: Error opening the watchdog device\n");
                      perror(wd_file);
                      return errno;
              }

              /*tmp will be loaded by the ioctl with the time left*/
              tmp = wd_period;
              ret = ioctl(wd_fd, WDIOC_SETTIMEOUT, &tmp);
              if (ret != 0) {
                      fprintf(stderr, "Kicker: Error initialising watchdog\n");
                      return ret;
              }
              fprintf(stderr, "Kicker: wd period set to %d s\n", tmp);
      }

      return ret;
}

static int register_to_dsme(dsmesock_connection_t *conn)
{
      DSM_MSGTYPE_HWWD_KICKER msg =
        DSME_MSG_INIT(DSM_MSGTYPE_HWWD_KICKER);

      /* Sending the register msg to dsme */
      return dsmesock_send(conn, &msg);
}

int main(void)
{
      static dsmesock_connection_t *dsme_conn;
      fd_set rfds;
      int ret;

      protect_from_oom();

      /* Connect to DSME */
      dsme_conn = dsmesock_connect();
      if (dsme_conn == 0) {
              fprintf(stderr, "Kicker: failed to connect to dsme, exiting...\n");
              return EXIT_FAILURE;
      }

      /* Register to DSME */
      if (register_to_dsme(dsme_conn) < 0) {
              fprintf(stderr, "Kicker: failed to register to DSME, exiting...\n");
              return EXIT_FAILURE;
      }

      /* Init WDs */
      if (init_wd() < 0) {
              fprintf(stderr, "Kicker: failed to init WDs, exiting...\n");
              return EXIT_FAILURE;
      }

      while (1) {
              FD_ZERO(&rfds);
              FD_SET(dsme_conn->fd, &rfds);
              dsmemsg_generic_t *msg;

              ret = select(dsme_conn->fd + 1, &rfds, NULL, NULL, NULL); 
              if (ret == -1) {
                  fprintf(stderr, "error in select()\n");
              } else if (ret == 0) {
                  /* should not happen */
                  fprintf(stderr, "Timeout!\n");
              } else {

                  msg = (dsmemsg_generic_t*)dsmesock_receive(dsme_conn);

                  if (DSMEMSG_CAST(DSM_MSGTYPE_HWWD_KICK, msg))
                    {

                        if (wd_enabled) {
                            /* Kicking WD */
                            if (write(wd_fd, "*\n", 2) != 2) {
                                fprintf(stderr, "Kicker: error kicking watchdog!\n");
                            }
                        }
                    }

                    free(msg);

                }
    }

    return EXIT_FAILURE;
}
