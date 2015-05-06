/**
   @file upstart.c

   DSME internal runlevel control using upstart D-Bus i/f
   <p>
   Copyright (C) 2010 Nokia Corporation.

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

#include <dbus/dbus.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <utmpx.h>
#include <sys/utsname.h>
#include <sys/time.h>


static bool save_state_for_getbootstate(dsme_runlevel_t runlevel);
static bool telinit_internal(dsme_runlevel_t runlevel);
static void shutdown_internal(dsme_runlevel_t runlevel);
static bool remount_mmc_readonly(void);


static int current_runlevel_in_utmp()
{
    int           runlevel = 0;
    struct utmpx  utmp;
    struct utmpx* utmp_runlevel;

    memset(&utmp, 0, sizeof utmp);
    utmp.ut_type = RUN_LVL;

    utmpxname(_PATH_UTMPX);
    setutxent();
    utmp_runlevel = getutxid(&utmp);
    if (utmp_runlevel) {
        runlevel = utmp_runlevel->ut_pid % 256;
        int prevlevel = utmp_runlevel->ut_pid / 256;
        dsme_log(LOG_DEBUG,
                 "read from utmp: %c (%d), %c (%d)",
                 prevlevel, prevlevel, runlevel, runlevel);
    } else {
        dsme_log(LOG_DEBUG, "could not read from utmp");
    }
    endutxent();

    return runlevel > 0 ? runlevel : 'N';
}

/*
 * since we are bypassing upstart's telinit,
 * we have to update utmp & wtmp by hand.
 * Notice that this is much simplified from
 * how upstart does it -- perhaps too much?
 */
static bool save_state_in_utmp_and_wtmp(dsme_runlevel_t runlevel)
{
    bool         saved = false;
    struct utmpx utmp;
    int          prevlevel = current_runlevel_in_utmp();

    runlevel += '0';

    // first populate the utmpx struct
    memset(&utmp, 0, sizeof utmp);
    utmp.ut_type = RUN_LVL;
    utmp.ut_pid  = runlevel + prevlevel * 256;
    strncpy(utmp.ut_line, "~",        sizeof utmp.ut_line);
    strncpy(utmp.ut_id,   "~~",       sizeof utmp.ut_id);
    strncpy(utmp.ut_user, "runlevel", sizeof utmp.ut_user);

    struct utsname uts;
    if (uname(&uts) == 0) {
        strncpy(utmp.ut_host, uts.release, sizeof utmp.ut_host);
    }

    struct timeval tv;
    gettimeofday(&tv, 0);
    utmp.ut_tv.tv_sec  = tv.tv_sec;
    utmp.ut_tv.tv_usec = tv.tv_usec;

    // then write the utmpx struct to both utmp & wtmp
    utmpxname(_PATH_UTMPX);
    setutxent();
    if (pututxline(&utmp)) {
        saved = true;
        dsme_log(LOG_DEBUG,
                 "saved to utmp: %c (%d), %c (%d)",
                 prevlevel, prevlevel, runlevel, runlevel);
    }
    endutxent();

    updwtmpx(_PATH_WTMPX, &utmp);

    return saved;
}


static bool save_state_for_getbootstate(dsme_runlevel_t runlevel)
{
    const char* state;
    int         fd;

    /* Write out saved state for getbootstate.
     * Prefer USER over ACT_DEAD over others. The order matters in case many
     * DSME states are mapped to the same runlevel. This really shouldn't be
     * the case when upstart is used, but still, be prepared for it.
     */
    if (runlevel == DSME_RUNLEVEL_USER) {
        state = "USER";
    }
    else if (runlevel == DSME_RUNLEVEL_ACTDEAD) {
        state = "ACT_DEAD";
    }
    else if (runlevel == DSME_RUNLEVEL_TEST) {
        state = "TEST";
    }
    else if (runlevel == DSME_RUNLEVEL_LOCAL) {
        state = "LOCAL";
    }
    else {
        state = 0;
    }

    if (state) {
#define SAVED_STATE_FILE "/var/lib/dsme/saved_state"
        (void)unlink (SAVED_STATE_FILE);

        while ((fd = open (SAVED_STATE_FILE, O_CREAT|O_WRONLY|O_SYNC, 00600))
                   == -1 && errno == EINTR)
        {
            /* EMPTY LOOP */
        }
        if (fd < 0) {
            return false;
        }

        int res;
        while (*state) {
            while ((res = write (fd, state, strlen(state)))
                        == -1 && errno == EINTR)
            {
                /* EMPTY LOOP */
            }
            if (res == -1) {
                /* don't leave partial state files behind */
                (void)unlink (SAVED_STATE_FILE);
                return false;
            } else {
                state += res;
            }
        }

        while ((res = close (fd)) == -1 && errno == EINTR) {
            /* EMPTY LOOP */
        }
        if (res == -1) {
            /* don't leave partial state files behind */
            (void)unlink (SAVED_STATE_FILE);
            return false;
        }
    }

    return true;
}

/**
   This function is used to tell init to change to new runlevel.
   @param new_state State corresponding to the new runlevel
   @return true on success, false on failure
   @todo Make sure that the runlevel change takes place
*/
// TODO: D-Bus marshalling is way ugly; needs to be abstracted
static bool telinit_internal(dsme_runlevel_t runlevel)
{
    bool            runlevel_changed = false;
    DBusError       error;
    DBusConnection* conn = 0;
    DBusMessage*    call = 0;
    DBusMessageIter iter;
    DBusMessageIter env_iter;

    dbus_error_init(&error);

    conn = dbus_connection_open_private("unix:abstract=/com/ubuntu/upstart", &error);
    if (!conn) {
        dsme_log(LOG_CRIT, "Cannot connect to upstart");
        goto done;
    }
    call = dbus_message_new_method_call(0,
                                        "/com/ubuntu/Upstart",
                                        "com.ubuntu.Upstart0_6",
                                        "EmitEvent");
    dbus_message_set_auto_start(call, false); // TODO: needed?

    dbus_message_iter_init_append(call, &iter);

    // marshal message name
    const char* name = "runlevel";
    if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &name)) {
        dsme_log(LOG_CRIT, "Cannot append to message");
        goto done;
    }

    // marshal environment, which includes the target runlevel
    if (!dbus_message_iter_open_container(&iter,
                                          DBUS_TYPE_ARRAY,
                                          "s",
                                          &env_iter))
    {
        dsme_log(LOG_CRIT, "Cannot append to message");
        goto done;
    } else {
        char* s = 0;
        if (asprintf(&s, "RUNLEVEL=%c", '0'+runlevel) == -1) {
            dsme_log(LOG_CRIT, "asprintf failed");
            dbus_message_iter_abandon_container(&iter, &env_iter);
            goto done;
        }
        if (!dbus_message_iter_append_basic(&env_iter, DBUS_TYPE_STRING, &s)) {
            dsme_log(LOG_CRIT, "Cannot append to message");
            dbus_message_iter_abandon_container(&iter, &env_iter);
            goto done;
        }
        free(s); // TODO: needed?
        s = 0; // TODO: needed?
        if (asprintf(&s, "HABA=tsuh") == -1) {
            dsme_log(LOG_CRIT, "asprintf failed");
            dbus_message_iter_abandon_container(&iter, &env_iter);
            goto done;
        }
        if (!dbus_message_iter_append_basic(&env_iter, DBUS_TYPE_STRING, &s)) {
            dsme_log(LOG_CRIT, "Cannot append to message");
            dbus_message_iter_abandon_container(&iter, &env_iter);
            goto done;
        }
        free(s); // TODO: needed?
        s = 0; // TODO: needed?
    }
    if (!dbus_message_iter_close_container(&iter, &env_iter)) {
        dsme_log(LOG_CRIT, "Cannot close container");
        goto done;
    }

    // marshal 'wait'
    int wait = false;
    if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_BOOLEAN, &wait)) {
        dsme_log(LOG_CRIT, "Cannot append to message");
        goto done;
    }

    if (!save_state_in_utmp_and_wtmp(runlevel))
    {
        dsme_log(LOG_CRIT, "Error saving state in utmp");
        goto done;
    }

    if (!save_state_for_getbootstate(runlevel)) {
        dsme_log(LOG_CRIT, "Error saving state for getbootstate");
        goto done;
    }


    // make the call
    // TODO: do not block!
    DBusMessage* reply;
    reply = dbus_connection_send_with_reply_and_block(conn, call, -1, &error);
    if (dbus_error_is_set(&error)) {
        dsme_log(LOG_CRIT, "D-Bus sending failed: %s", error.message);
        goto done;
    }

    // inspect the response
    if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
        dsme_log(LOG_CRIT, "Got an error reply");
        dbus_message_unref(reply);
        goto done;
    }
    if (dbus_message_get_type(reply) != DBUS_MESSAGE_TYPE_METHOD_RETURN) {
        dsme_log(LOG_CRIT, "Got an unknown reply type");
        dbus_message_unref(reply);
        goto done;
    }

    // TODO: huh? should we check the reply?
    runlevel_changed = true;

    if (reply) dbus_message_unref(reply);


done:
    if (call) dbus_message_unref(call);
    if (conn) {
        dbus_connection_close(conn);
        dbus_connection_unref(conn);
    }
    dbus_error_free(&error);

    return runlevel_changed;
}


/*
 * This function will do the shutdown or reboot (based on desired runlevel).
 * In case of failure, function will shutdown/reboot by itself.
 */
static void shutdown_internal(dsme_runlevel_t runlevel)
{
  if ((runlevel != DSME_RUNLEVEL_REBOOT)   &&
      (runlevel != DSME_RUNLEVEL_SHUTDOWN) &&
      (runlevel != DSME_RUNLEVEL_MALF))
  {
      dsme_log(LOG_DEBUG, "Shutdown request to bad runlevel (%d)", runlevel);
      return;
  }
  dsme_log(LOG_CRIT,
           runlevel == DSME_RUNLEVEL_SHUTDOWN ? "Shutdown" :
           runlevel == DSME_RUNLEVEL_REBOOT   ? "Reboot"   :
                                                "Malf");

  /* If runlevel change fails, handle the shutdown/reboot by DSME */
  if (!telinit_internal(runlevel))
  {
      dsme_log(LOG_CRIT, "Doing forced shutdown/reboot");
      sync();

      (void)remount_mmc_readonly();

      if (runlevel == DSME_RUNLEVEL_SHUTDOWN ||
          runlevel == DSME_RUNLEVEL_MALF)
      {
          dsme_log(LOG_CRIT, "Issuing poweroff");
          if (system("/sbin/poweroff") != 0) {
              dsme_log(LOG_ERR, "/sbin/poweroff failed, trying again in 3s");
              sleep(3);
              if (system("/sbin/poweroff") != 0) {
                  dsme_log(LOG_ERR, "/sbin/poweroff failed again");
                  goto fail_and_exit;
              }
          }
      } else {
          dsme_log(LOG_CRIT, "Issuing reboot");
          if (system("/sbin/reboot") != 0) {
              dsme_log(LOG_ERR, "/sbin/reboot failed, trying again in 3s");
              sleep(3);
              if (system("/sbin/reboot") != 0) {
                  dsme_log(LOG_ERR, "/sbin/reboot failed again");
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
          dsme_log(LOG_CRIT, "fork failed, no way to remount");
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
  (void)telinit_internal(msg->runlevel);
}

DSME_HANDLER(DSM_MSGTYPE_SHUTDOWN, conn, msg)
{
  shutdown_internal(msg->runlevel);
}


module_fn_info_t message_handlers[] = {
  DSME_HANDLER_BINDING(DSM_MSGTYPE_CHANGE_RUNLEVEL),
  DSME_HANDLER_BINDING(DSM_MSGTYPE_SHUTDOWN),
  { 0 }
};


void module_init(module_t* module)
{
  dsme_log(LOG_DEBUG, "upstart.so loaded");
}

void module_fini(void)
{
  dsme_log(LOG_DEBUG, "upstart.so unloaded");
}
