/**
   @file runlevel.c

   DSME internal runlevel control
   <p>
   Copyright (C) 2009-2010 Nokia Corporation.

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

#ifndef __cplusplus
#define _GNU_SOURCE
#endif

#include "runlevel.h"
#include "../include/dsme/modules.h"
#include "../include/dsme/logging.h"
#include "../include/dsme/modulebase.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>

static bool change_runlevel(dsme_runlevel_t runlevel);
static bool remount_mmc_readonly(void);

/**
   This function is used to tell init to change to new runlevel.
   Currently telinit is used.
   @param new_state State corresponding to the new runlevel
   @return Returns the return value from system(), -1 == error
   @todo Make sure that the runlevel change takes place
*/
static bool change_runlevel(dsme_runlevel_t runlevel)
{
  char command[32];

  if (access("/sbin/telinit", X_OK) == 0) {
      snprintf(command, sizeof(command), "/sbin/telinit %i", runlevel);
  } else if (access("/usr/sbin/telinit", X_OK) == 0) {
      snprintf(command, sizeof(command), "/usr/sbin/telinit %i", runlevel);
  } else {
      return false;
  }
  dsme_log(LOG_NOTICE, "Issuing %s", command);

  if (system(command) != 0) {
      dsme_log(LOG_CRIT, "failed to change runlevel, trying again in 2s");
      sleep(2);
      return system(command) == 0;
  }

  return true;
}


/*
 * This function will do the shutdown or reboot (based on desired runlevel).
 * If the telinit is present, runlevel change is requested.
 * Otherwise function will shutdown/reboot by itself.
 * TODO: How to make sure runlevel transition work
 * TODO: Is checking telinit reliable enough?
 */
static void shutdown(dsme_runlevel_t runlevel)
{
  char command[64];

  if ((runlevel != DSME_RUNLEVEL_REBOOT)   &&
      (runlevel != DSME_RUNLEVEL_SHUTDOWN) &&
      (runlevel != DSME_RUNLEVEL_MALF))
  {
      dsme_log(LOG_WARNING, "Shutdown request to bad runlevel (%d)", runlevel);
      return;
  }
  dsme_log(LOG_NOTICE,
           runlevel == DSME_RUNLEVEL_SHUTDOWN ? "Shutdown" :
           runlevel == DSME_RUNLEVEL_REBOOT   ? "Reboot"   :
                                                "Malf");

  /* If we have systemd, use systemctl commands */
  if (access("/bin/systemctl", X_OK) == 0) {
      if (runlevel == DSME_RUNLEVEL_SHUTDOWN) {
          snprintf(command, sizeof(command), "/bin/systemctl --no-block poweroff");
      } else if (runlevel == DSME_RUNLEVEL_REBOOT) {
          snprintf(command, sizeof(command), "/bin/systemctl --no-block reboot");
      } else {
          dsme_log(LOG_WARNING, "MALF not supported by our systemd implementation");
	  goto fail_and_exit;
      }
      dsme_log(LOG_NOTICE, "Issuing %s", command);
      if (system(command) != 0) {
          dsme_log(LOG_WARNING, "command %s failed: %m", command);
          /* We ignore error. No retry or anything else */
      }
  }
  /* If runlevel change fails, handle the shutdown/reboot by DSME */
  else if (!change_runlevel(runlevel))
  {
      dsme_log(LOG_CRIT, "Doing forced shutdown/reboot");
      sync();

      (void)remount_mmc_readonly();

      if (runlevel == DSME_RUNLEVEL_SHUTDOWN ||
          runlevel == DSME_RUNLEVEL_MALF)
      {
          if (access("/sbin/poweroff", X_OK) == 0) {
              snprintf(command, sizeof(command), "/sbin/poweroff");
          } else {
              snprintf(command, sizeof(command), "/usr/sbin/poweroff");
          }
          dsme_log(LOG_CRIT, "Issuing %s", command);
          if (system(command) != 0) {
	    dsme_log(LOG_ERR, "%s failed, trying again in 3s", command);
              sleep(3);
              if (system(command) != 0) {
		dsme_log(LOG_ERR, "%s failed again", command);
                  goto fail_and_exit;
              }
          }
      } else {
          if (access("/sbin/reboot", X_OK) == 0) {
              snprintf(command, sizeof(command), "/sbin/reboot");
          } else {
              snprintf(command, sizeof(command), "/usr/sbin/reboot");
          }
          dsme_log(LOG_CRIT, "Issuing %s", command);
          if (system(command) != 0) {
	    dsme_log(LOG_ERR, "%s failed, trying again in 3s", command);
              sleep(3);
              if (system(command) != 0) {
		dsme_log(LOG_ERR, "%s failed again", command);
                  goto fail_and_exit;
              }
          }
      }

  }

  return;

fail_and_exit:
  dsme_log(LOG_CRIT, "Closing to clean-up!");
  dsme_exit(EXIT_FAILURE);
}


/*
 * This function tries to find mounted MMC (mmcblk) and remount it
 * read-only if mounted. 
 * @return true on success, false on failure
 */
static bool remount_mmc_readonly(void)
{
  bool   mounted = false;
  char*  args[] = { (char*)"mount", NULL, NULL, (char*)"-o", (char*)"remount,ro", 0 };
  char   device[256];
  char   mntpoint[256];
  char*  line = NULL;
  size_t len = 0;
  FILE*  mounts_file = NULL;

  /* Let's try to find the MMC in /proc/mounts */
  mounts_file = fopen("/proc/mounts", "r");
  if (!mounts_file) {
      dsme_log(LOG_WARNING, "Can't open /proc/mounts. Leaving MMC as is");
      return false;
  }

  while (getline(&line, &len, mounts_file) != -1) {
      if (strstr(line, "mmcblk")) {
          sscanf(line, "%s %s", device, mntpoint);
          mounted = true;
      }
  }

  if (line) {
      free(line);
      line = NULL;
  }
  fclose(mounts_file);

  /* If mmc was found, try to umount it */
  if (mounted) {
      int   status = -1;
      pid_t pid;
      pid_t rc;

      dsme_log(LOG_WARNING, "MMC seems to be mounted, trying to mount read-only (%s %s).", device, mntpoint);

      args[1] = (char*)&device;
      args[2] = (char*)&mntpoint;
      /* try to remount read-only */
      if ((pid = fork()) < 0) {
          dsme_log(LOG_CRIT, "fork failed, exiting");
          return false;
      } else if (pid == 0) {
          execv("/bin/mount", args);
          execv("/sbin/mount", args);

          dsme_log(LOG_ERR, "remount failed, no mount cmd found");
          return false;
      } 
      while ((rc = wait(&status)) != pid)
          if (rc < 0 && errno == ECHILD)
              break;
      if (rc != pid || WEXITSTATUS(status) != 0) {
          dsme_log(LOG_ERR, "mount return value != 0, no can do.");
          return false;
      }

      dsme_log(LOG_NOTICE, "MMC remounted read-only");
      return true;

  } else {
      dsme_log(LOG_NOTICE, "MMC not mounted");
      return true;
  }
}


DSME_HANDLER(DSM_MSGTYPE_CHANGE_RUNLEVEL, conn, msg)
{
  (void)change_runlevel(msg->runlevel);
}

DSME_HANDLER(DSM_MSGTYPE_SHUTDOWN, conn, msg)
{
  shutdown(msg->runlevel);
}


module_fn_info_t message_handlers[] = {
  DSME_HANDLER_BINDING(DSM_MSGTYPE_CHANGE_RUNLEVEL),
  DSME_HANDLER_BINDING(DSM_MSGTYPE_SHUTDOWN),
  { 0 }
};


void module_init(module_t* module)
{
  dsme_log(LOG_DEBUG, "runlevel.so loaded");
}

void module_fini(void)
{
  dsme_log(LOG_DEBUG, "runlevel.so unloaded");
}
