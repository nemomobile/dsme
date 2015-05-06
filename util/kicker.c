/**
   @file kicker.c

   This program kicks watchdogs when it gets permission from DSME.
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
#include "../modules/hwwd.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

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

#include "../dsme-rd-mode.h"

#define OOM_ADJ_VALUE -17


typedef struct wd_t {
    const char* file;   /* pathname of the watchdog device */
    int         period; /* watchdog timeout; 0 for keeping the default */
} wd_t;

static const wd_t wd[] = {
    { "/dev/watchdog",    0 },
    { "/dev/twl4030_wdt", 0 }
};
#define WD_COUNT (sizeof(wd) / sizeof(wd[0]))
static int wd_fd[WD_COUNT];

static bool wd_enabled = true;



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

static int read_rd_mode_config(void)
{
      unsigned long len = 0;
      const char *p;

      p = dsme_rd_mode_get_flags();
      if (p) {
              fprintf(stderr, "R&D mode enabled\n");

              len = strlen(p);
              if (len > 1) {
                      if (strstr(p, "no-omap-wd")) {
                              wd_enabled = false;
                              fprintf(stderr, "WD kicking disabled\n");
                      } else {
                              wd_enabled = true;
                      }
              } else {
                      wd_enabled = true;
                      fprintf(stderr, "No WD flags found, kicking enabled!\n");
              }
      }

      return 0;
}

static bool init_wd(void)
{
      int i;
      for (i = 0; i < WD_COUNT; ++i) {
          wd_fd[i] = -1;
      }

      read_rd_mode_config();
      if (!wd_enabled) {
          return false;
      }

      if (wd_enabled) {
          for (i = 0; i < WD_COUNT; ++i) {
              wd_fd[i] = open(wd[i].file, O_RDWR);
              if (wd_fd[i] == -1) {
                      fprintf(stderr,
                              "Kicker: Error opening watchdog %s\n",
                              wd[i].file);
                      perror(wd[i].file);
              } else if (wd[i].period != 0) {
                  /* set the wd period */
                  /* ioctl() will overwrite tmp with the time left */
                  int tmp = wd[i].period;
                  if (ioctl(wd_fd[i], WDIOC_SETTIMEOUT, &tmp) != 0) {
                      fprintf(stderr,
                              "Kicker: Error initialising watchdog %s\n",
                              wd[i].file);
                  } else {
                      fprintf(stderr, "Kicker: wd period set to %d s\n", tmp);
                  }
              }
          }
      }

      return true;
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
      if (!init_wd()) {
              fprintf(stderr, "Kicker: failed to init WDs, exiting...\n");
              return EXIT_FAILURE;
      }

      while (true) {
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
                            int i;
                            for (i = 0; i < WD_COUNT; ++i) {
                                if (wd_fd[i] != -1) {
                                    /* Kick WD */
                                    if (write(wd_fd[i], "*", 1) != 1) {
                                        fprintf(
                                            stderr,
                                            "Kicker: error kicking watchdog!\n");
                                    }
                                }
                            }
                        }
                    }

                    free(msg);

                }
    }

    return EXIT_FAILURE;
}
