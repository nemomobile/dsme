/**
   @file processwd.c

   This file implements software watchdog for processes that want to use it.
   Proceses that do not respond to the request will be killed by signal 9.
   <p>
   Copyright (C) 2004-2010 Nokia Corporation.

   @author Ismo Laitinen <ismo.laitinen@nokia.com>
   @author Ari Saastamoinen
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


/**
 * @defgroup modules DSME Modules
 */

/**
 * @defgroup processwd Process watchdog 
 * @ingroup modules
 *
 */

#define _XOPEN_SOURCE

#include <iphbd/iphb_internal.h>

#include "../include/dsme/modules.h"
#include "../include/dsme/logging.h"
#include "../include/dsme/timers.h"

#include <dsme/messages.h>
#include <dsme/processwd.h>

#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <signal.h>


/**
 * @ingroup processwd
 * Defines how may pings need to be ignored before the process is killed.
 */
#define MAXPING 3  /* Process wil be killed at ping #3 */

/**
 * @ingroup processwd
 * Defines the time given to a nonresponsive process between SIGABRT and SIGKILL.
 */
#define ABORT_GRACE_PERIOD_SECONDS 2


static void ping_all(void);
static void subscribe_to_wakeup(void);


typedef struct {
    pid_t        pid;
    int          pingcount;
    endpoint_t*  client;
    dsme_timer_t kill_timer;
} dsme_swwd_entry_t;


static GSList* processes = 0;


static dsme_swwd_entry_t* swwd_entry_new(pid_t pid, endpoint_t* client)
{
  dsme_swwd_entry_t* proc =
    (dsme_swwd_entry_t*)malloc(sizeof(dsme_swwd_entry_t));
  if (proc == NULL) {
      dsme_log(LOG_CRIT, "%s", strerror(errno));
  } else {
      proc->pid        = pid;
      proc->pingcount  = 0;
      /* TODO: it is a bit risky to copy an endpoint */
      proc->client     = endpoint_copy(client);
      proc->kill_timer = 0;
  }

  return proc;
}

static void swwd_entry_delete(dsme_swwd_entry_t * proc)
{
  if (proc) {
      if (proc->kill_timer) {
          dsme_destroy_timer(proc->kill_timer);
          dsme_log(LOG_NOTICE, "killing process (pid: %i)", proc->pid);
          kill(proc->pid, SIGKILL); 
      }
      endpoint_free(proc->client);
      free(proc);
  }
}

static gint compare_pids(gconstpointer a, gconstpointer b)
{
  const dsme_swwd_entry_t *proc = (dsme_swwd_entry_t *)a;
  pid_t                    pid  = GPOINTER_TO_INT(b);

  /* Should not overflow, and for use with g_slist_find_custom()
   * it is enough if we get zero vs non-zero correctly */
  return proc->pid - pid;
}

static gint compare_endpoints(gconstpointer proc, gconstpointer client)
{
  /* Note: returns 0/1 instead of -1/0/+1, but this should be
   *       enough for use with g_slist_find_custom() */
  return !endpoint_same(((dsme_swwd_entry_t*)proc)->client, client);
}


static int abort_timeout_func(void* data)
{
  GSList* node;

  node = g_slist_find_custom(processes, data, compare_pids);
  if (node != NULL) {
      /* the process has not been removed yet; kill it */
      pid_t              pid  = GPOINTER_TO_INT(data);
      dsme_swwd_entry_t* proc = (dsme_swwd_entry_t*)(node->data);

      proc->kill_timer = 0; /* the timer has expired */

      dsme_log(LOG_NOTICE, "killing process (pid: %i)", pid);
      kill(pid, SIGKILL); 

      swwd_entry_delete(proc);
      processes = g_slist_delete_link(processes, node);
  }

  return 0; /* stop the interval */
}

DSME_HANDLER(DSM_MSGTYPE_WAKEUP, conn, msg)
{
    dsme_log(LOG_DEBUG, "processwd: ping");
    ping_all();

    subscribe_to_wakeup();
}

static void ping_all(void)
{
    GSList* node;
    GSList* next;

    for (node = processes; node; node = next) {
        dsme_swwd_entry_t* proc;

        next = g_slist_next(node);

        proc = (dsme_swwd_entry_t *)(node->data);
        proc->pingcount++;

        /* Is it pinged too many times ? */
        if (proc->pingcount == MAXPING) {
            if (proc->kill_timer == 0) {
                dsme_log(LOG_ERR, "process (pid: %i) not responding to processwd pings," 
                         " aborting it...", proc->pid);
                /* give the nonresponsive process chance to abort... */
                kill(proc->pid, SIGABRT);

                /* ...but make sure to kill it after a grace period */
                proc->kill_timer = dsme_create_timer(ABORT_GRACE_PERIOD_SECONDS,
                                                     abort_timeout_func,
                                                     GINT_TO_POINTER(proc->pid));
                if (proc->kill_timer == 0) {
                    /* timer creation failed; kill the process immediately */
                    dsme_log(LOG_ERR, "...kill due to timer failure: %s", strerror(errno));
                    kill(proc->pid, SIGKILL);

                    swwd_entry_delete(proc);
                    processes = g_slist_delete_link(processes, node);
                }
            }
        } else {
            DSM_MSGTYPE_PROCESSWD_PING msg =
              DSME_MSG_INIT(DSM_MSGTYPE_PROCESSWD_PING);

            msg.pid = proc->pid;
            endpoint_send(proc->client, &msg);
            dsme_log(LOG_DEBUG, "sent ping to pid %i", proc->pid);
        }
    }
}

static void subscribe_to_wakeup(void)
{
    DSM_MSGTYPE_WAIT msg = DSME_MSG_INIT(DSM_MSGTYPE_WAIT);
    msg.req.mintime = 24;
    msg.req.maxtime = 30;
    msg.req.pid     = 0;
    msg.data        = 0;

    broadcast_internally(&msg);
}

/**
 * Function handles setting a new process to be watchdogged.
 */
DSME_HANDLER(DSM_MSGTYPE_PROCESSWD_CREATE, client, msg)
{
  dsme_swwd_entry_t* proc;

  if (g_slist_find_custom(processes, GINT_TO_POINTER(msg->pid), compare_pids)) {
      /* Already there - just ignore and return */
      dsme_log(LOG_DEBUG, "Process WD requested for existing pid\n");
      return;
  }

  proc = swwd_entry_new(msg->pid, client);
  if (!proc) {
      return;
  }

  processes = g_slist_prepend(processes, proc);

  dsme_log(LOG_DEBUG, "Added process (pid: %i) to processwd", proc->pid);
}

/**
 * Function handles the pong sent by process. Ping count is resetted.
 */
DSME_HANDLER(DSM_MSGTYPE_PROCESSWD_PONG, conn, msg)
{
  GSList*            node;
  dsme_swwd_entry_t* proc;

  node = g_slist_find_custom(processes, GINT_TO_POINTER(msg->pid), compare_pids);
  if (!node) {
      /* Already there - just ignore and return */
      dsme_log(LOG_WARNING, "ProcessWD PONG for non-existing pid %i", msg->pid);
      return;
  }

  proc = (dsme_swwd_entry_t *)(node->data);
  if (proc) {
      dsme_log(LOG_DEBUG,
               "pong for pid %i after %d ping(s)",
               msg->pid,
               proc->pingcount);
      proc->pingcount = 0;
  }
}

/**
 * Function deletes the watchdog for process.
 */
static void swwd_del(pid_t pid)
{
  GSList*            node;
  dsme_swwd_entry_t* proc;

  node = g_slist_find_custom(processes, GINT_TO_POINTER(pid), compare_pids);
  if (!node) {
      dsme_log(LOG_DEBUG,
               "no process registered to use processwd with pid %i",
               pid);
      /* Nothing to delete - just ignore and return */
      return;
  }

  proc = (dsme_swwd_entry_t*)(node->data);
  swwd_entry_delete(proc);
  processes = g_slist_delete_link(processes, node);
  dsme_log(LOG_DEBUG, "removed exited process (pid %d) from process wd", pid);
}

DSME_HANDLER(DSM_MSGTYPE_PROCESSWD_DELETE, conn, msg)
{
  swwd_del(msg->pid);
}

/**
 * 	If socket closed remove it from checking list 
 */
DSME_HANDLER(DSM_MSGTYPE_CLOSE, conn, msg)
{
  GSList*            node;
  GSList*            deleted;
  dsme_swwd_entry_t* proc;

  node = processes;
  while ((node = g_slist_find_custom(node, conn, compare_endpoints))) {
      if (node->data) {
          proc = (dsme_swwd_entry_t *)(node->data);
          dsme_log(LOG_DEBUG, "process socket closed, killing process with pid %i", proc->pid);
          kill(proc->pid, SIGKILL);
          swwd_entry_delete(proc);
      }
      deleted = node;
      node = g_slist_next(node);
      processes = g_slist_delete_link(processes, deleted);
      dsme_log(LOG_DEBUG, "removed process with closed socket from process wd");
  }
}

/**
 * @ingroup processwd
 * DSME messages handled by process watchdog.
 * - DSM_MSGTYPE_PROCESSWD_CREATE Starts a process watchdog for connection sending
 * the event. dsmemsg_swwd_t
 * - DSM_MSGTYPE_PROCESSWD_DELETE Stop the watchdog for connection sending the
 *   event. dsmemsg_swwd_t
 * - DSM_MSGTYPE_PONG The reply sent by a process for ping. dsmemsg_swwd_t
 *   dsmemsg_timeout_change_t
 */ 
module_fn_info_t message_handlers[] = {
      DSME_HANDLER_BINDING(DSM_MSGTYPE_PROCESSWD_CREATE),
      DSME_HANDLER_BINDING(DSM_MSGTYPE_PROCESSWD_DELETE),
      DSME_HANDLER_BINDING(DSM_MSGTYPE_PROCESSWD_PONG),
      DSME_HANDLER_BINDING(DSM_MSGTYPE_WAKEUP),
      DSME_HANDLER_BINDING(DSM_MSGTYPE_CLOSE),
      {0}
};

void module_init(module_t *handle)
{
  dsme_log(LOG_DEBUG, "processwd.so loaded");

  subscribe_to_wakeup();
}

void module_fini(void)
{
  /* Free list */
  while (processes) {
    swwd_entry_delete((dsme_swwd_entry_t*)(processes->data));
    processes = g_slist_delete_link(processes, processes);
  }

  // TODO: cancel wakeup subscription

  dsme_log(LOG_DEBUG, "processwd.so unloaded");
}
