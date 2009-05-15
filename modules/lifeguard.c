/**
   @file lifeguard.c

   This file implements process starting and stopping feature.
   This module also listens to process quit messages and reacts
   to them according to the policy set when process was started.
   <p>
   Copyright (C) 2004-2009 Nokia Corporation.

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
 * @defgroup lifeguard Lifeguard module 
 * @ingroup modules
 *
 */

#ifndef __cplusplus
#define _GNU_SOURCE
#endif

#include "lifeguard.h"

#include "state.h"
#include "spawn.h"
#include "dsme/messages.h"
#include "dsme/modules.h"
#include "dsme/logging.h"

#include <cal.h>

#include <glib.h>
#include <errno.h>
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <sys/wait.h>
#include <pwd.h>
#include <grp.h>
#include <sysexits.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <stdbool.h>


/**
 * @ingroup lifeguard
 * File to read UIDs allowed to use reset policy in addition to root
 */
#define FILE_LIFEGUARD_UIDS   "/etc/dsme/lifeguard.uids"

#define FILE_DSME_DIR         "/var/lib/dsme"
#define FILE_DSME_LG_RESTARTS "/var/lib/dsme/stats/lifeguard_restarts"
#define FILE_DSME_LG_RESETS   "/var/lib/dsme/stats/lifeguard_resets"

#define DELIMETER             " : "

#define FILE_REBOOT_OVERRIDE  "/etc/no_lg_reboots"

static char lg_reboot_enabled = 1;


/**
 * A list node in process list. Used to track which processes are asked to be
 * started. Contains also the policy for process exit and possible retry count.
 */
typedef struct {
	char*             command;
	pid_t             pid;
	uid_t             uid;
	gid_t             gid;
	int               nice;
	time_t            starttime;
	GSList*           node;
	process_actions_t action;
	char**            env;
	int               first_restart_time;
	int               restart_count;
	int               restart_limit;
	int               restart_period;
} dsme_process_t;


static int deleteprocess(dsme_process_t * process);
static void send_reset_request();
static char **getenvbypid(pid_t pid);
static void read_priv_uids(void);
static int increment_process_counter(const char* statfilename, const char* process);
static int update_reset_count(const char* process);
static int update_restart_count(const char* process);


/**
 * List of processes registered to Lifeguard
 */
static GSList* processes = 0;

/**
 * List of UIDs priviledged to use reset policy
 */
static GSList* uids = 0;


static bool scan_uid(const char* line, uid_t* uid)
{
  bool found = false;
  int  iuid;

  if (sscanf(line, "%i", &iuid) == 1) {
      if (uid) {
          *uid = iuid;
      }
      found = true;
  }

  return found;
}

static bool map_name_to_uid(const char* name, uid_t* uid)
{
  bool           found = false;
  struct passwd* passwd;

  if ((passwd = getpwnam(name))) {
      if (uid) {
          *uid = passwd->pw_uid;
      }
      found = true;
  }

  return found;
}

static void read_priv_uids(void)
{
  FILE*  file = NULL;
  size_t len  = 0;
  char*  line = NULL;
  uid_t  uid  = -1;

  file = fopen(FILE_LIFEGUARD_UIDS, "r");
  if (!file) {
      dsme_log(LOG_ERR,
               "error opening %s, "
                 "only root can use reset policy in Lifeguard",
               FILE_LIFEGUARD_UIDS);
      return;
  }

  while (getline(&line, &len, file) > 0) {
      char* nl;

      if (line[0] == '#') {
          continue;
      }
      if ((nl = strrchr(line, '\n'))) {
          *nl = '\0';
      }

      if (scan_uid(line, &uid) ||
          map_name_to_uid(line, &uid))
        {
          dsme_log(LOG_DEBUG, "Got UID: %i, adding to list..", uid);	
          uids = g_slist_prepend(uids, (void*)uid);
        }

  }
  if (line)
      free(line);
  fclose(file);
}


/**
   Own version on realloc().
   Original realloc() do not touch original memory area if error occurs.

   @param ptr  Pointer to previously allocated memory area
   @param size size of new area
   @return pointer to newly allocated area,
           or NULL in error
*/
static inline void* myrealloc(void* ptr, size_t size) 
{
	void* tmp;

	tmp = realloc(ptr, size);
	if (!tmp) {
		dsme_log(LOG_ERR, "%s", strerror(errno));
		free(ptr);
	}
	return tmp;
}

/**
 * Prevent further respawn of processes. Change their action to ONCE
 */
static void set_action(gpointer proc, gpointer action)
{
  ((dsme_process_t*)proc)->action = (process_actions_t)action;
}

DSME_HANDLER(DSM_MSGTYPE_STATE_CHANGE_IND, conn, msg)
{
  /* stop monitoring in lifeguard if we are shutting down or rebooting */
  if (msg->state == DSME_STATE_SHUTDOWN || msg->state == DSME_STATE_REBOOT) {
      /* Check permissions */
      const struct ucred* ucred = endpoint_ucred(conn);

      if (!ucred) {
          dsme_log(LOG_ERR, "getucred failed");
          return;
      }
      if (ucred->uid != 0) {
          return;
      }

      /* Traverse through process list and change every action as ONCE */
      g_slist_foreach(processes, set_action, (gpointer)ONCE);
  }
}

/** 
 * This function frees process data 
 */
static int deleteprocess(dsme_process_t* process)
{
  if (process) {
      if (process->env) {
          free(process->env);
      }
      if (process->command) {
          free(process->command);
      }
      if (process->node) {
          processes = g_slist_delete_link(processes, process->node);
      }
      free(process);
  }
  return 0;
}

/**
   This function get all environmental variables from given process.
   Note: This is linux specific.

   @arg pid Process id where envs are taken from
   @return NULL terminated array of string pointers,
           or NULL in error
*/
static char** getenvbypid(pid_t pid)
{
	char** env      = NULL;
	size_t envsize  = 0;
	int    envcount = 0;

	char*  buffer   = NULL;
	size_t bufsize  = 0;
	size_t buflen   = 0;

	int    fd       = -1;
	char   filename[32];  /* Max len is strlen("/proc/65535/environ") */
	char*  ptr;

	int    i;

	if (!pid) goto EXIT;
	sprintf(filename,"/proc/%d/environ", pid);
	fd = open(filename, O_RDONLY);
	if (fd == -1) {
		dsme_log(LOG_ERR, "%s", strerror(errno));
		goto EXIT;
	}
	
	/* Read proc-file */
	while (1) {
		int len;
		
		if (bufsize <= buflen) {
			bufsize += 4096;
			buffer = (char*)myrealloc(buffer, bufsize);
			if (!buffer) goto EXIT;
		}
		
		len = read(fd, buffer+buflen, bufsize-buflen);
		if (len == -1) {
			dsme_log(LOG_ERR, "%s", strerror(errno));
			goto EXIT;
		}
		buflen += len;
		
		if (len == 0) {  /* EOF */
			/* Add double '\0' after buffer */
			if (buflen + 2 > bufsize) { /* Include 2 * '\0' */
				buffer = (char*)myrealloc(buffer, bufsize + 2);
				if (!buffer) goto EXIT;
			}
			*(buffer+buflen) = '\0';
			*(buffer+buflen+1) = '\0'; /* Just in case */

			break;
		}
	}
	

	/* make pointers */
	ptr = buffer;
	while (1) {

		/* +2 => Include terminating NULL as well */
		if (envsize < ((envcount+2) * sizeof(char*))) {
			envsize += sizeof(char*) * 1024;
			env = (char**)myrealloc(env, envsize);
			if (!env) goto EXIT;
		}			
		if (*ptr) {
			env[envcount++] = ptr;
		}
		env[envcount] = NULL;

		if (*ptr == '\0') break;

		while (*ptr++) ;  /* Find start of next string */
	}		
	envcount++;

	/* Add strings after pointer array */
	env = (char**)myrealloc(env, (envcount * sizeof(char*)) + (buflen + 2));
	if (!env) goto EXIT;
	memmove(&env[envcount], buffer, buflen+2);

	/* Relocate buffers to match memmoved addresses */
	for (i=0 ; env[i] ; i++) {
		env[i] = (char*)env[i] - 
			 (char*)buffer +
			 (char*)&env[envcount];
	}
 EXIT:
	if (buffer) free(buffer);
	if (fd != -1) close(fd);
	return env;
}


static gint compare_commands(gconstpointer proc, gconstpointer command)
{
  return strcmp(((dsme_process_t*)proc)->command, command);
}

/**
 * This function starts the requested process.
 * @todo Potential security hole.
   Is sizeof(msg) always bigger or equal compared to msg->size ?  
 */
DSME_HANDLER(DSM_MSGTYPE_PROCESS_START, client, msg)
{
  DSM_MSGTYPE_PROCESS_STARTSTATUS returnmsg =
      DSME_MSG_INIT(DSM_MSGTYPE_PROCESS_STARTSTATUS);
  dsme_process_t*     process = NULL;
  const struct ucred* ucred;
  int                 error   = -1;
  const char*         command;
  size_t              command_size;

  dsme_log(LOG_DEBUG, "Lifeguard start request received..");

  /* make sure command is NULL terminated */
  command      = (const char*)DSMEMSG_EXTRA(msg);
  command_size = DSMEMSG_EXTRA_SIZE(msg);
  if (command      == 0 ||
      command_size == 0 ||
      command[command_size-1] != '\0')
  {
      return;
  }

  /* refuse to start the same process again */
  if (g_slist_find_custom(processes, command, compare_commands)) {
      error = 1;
      dsme_log(LOG_ERR,
               "Lifeguard refused to start command (%s) again",
               command);
      goto cleanup;
  }

  process = (dsme_process_t*)malloc(sizeof(dsme_process_t));
  if (!process) {
      dsme_log(LOG_CRIT, "%s", strerror(errno));
      goto cleanup;
  }
  memset(process, 0, sizeof(dsme_process_t));

  ucred = endpoint_ucred(client);
  if (!ucred) {
      dsme_log(LOG_ERR, "Cannot get ucred");
      goto cleanup;
  }

  process->env = getenvbypid(ucred->pid);
  process->action = msg->action;
  process->restart_limit = msg->restart_limit;
  process->restart_period = msg->restart_period;

  /* set uid */
  if (msg->uid != ucred->uid && ucred->uid == 0)
      process->uid = msg->uid;
  else
      process->uid = ucred->uid;
  /* set gid */
  if (msg->gid != ucred->gid && ucred->uid == 0)
      process->gid = msg->gid;
  else
      process->gid = ucred->gid;

  /* set nice */
  if (msg->nice >= -1 || ucred->uid == 0)
      process->nice = msg->nice;
  else
      process->nice = 0;

  if (process->action == RESET && ucred->uid != 0) {
      if (g_slist_find(uids, (void*)process->uid)) {
          /* No permission to set RESET policy */
          dsme_log(LOG_ERR, "No permission to set RESET policy for Lifeguard");
          error = EX_NOPERM;
          goto cleanup;
      }
  }

  process->command = strdup(command);

  if (!process->command) {
      dsme_log(LOG_ERR, "%s", strerror(errno));
      goto cleanup;
  }

  dsme_log(LOG_DEBUG, "Trying to start: %s", process->command);
  process->pid =
    spawn_proc(process->command, process->uid, process->gid, process->nice, process->env);
  if (!process->pid) {
      error = errno;
      returnmsg.pid = 0;
  } else {
      /* success; must not wait for process start or process exit */
      error = 0;
      returnmsg.pid = process->pid;
  }

  if (!error) {
      processes = g_slist_prepend(processes, process);
      process->node = processes;
      dsme_log(LOG_INFO,
               "process '%s' started with pid %d",
               process->command,
               process->pid);
      process = NULL;
  }

cleanup:
  returnmsg.status = error;

  endpoint_send(client, &returnmsg);

  deleteprocess(process);
}

DSME_HANDLER(DSM_MSGTYPE_PROCESS_STOP, client, msg)
{
  DSM_MSGTYPE_PROCESS_STOPSTATUS returnmsg =
      DSME_MSG_INIT(DSM_MSGTYPE_PROCESS_STOPSTATUS);
  returnmsg.killed = false;
  const char* info = "not found, not root or kill failed";

  GSList*             ptr;
  const struct ucred* ucred;
  const char*         command;
  size_t              command_size;

  ucred = endpoint_ucred(client);
  if (!ucred) {
      info = "failed to get ucred";
      goto cleanup;
  }

  /* make sure that command is null terminated */
  command      = (const char*)DSMEMSG_EXTRA(msg);
  command_size = DSMEMSG_EXTRA_SIZE(msg);
  if (command      == 0 ||
      command_size == 0 ||
      command[command_size-1] != '\0')
  {
      info = "non-terminated command string";
      goto cleanup;
  }
  dsme_log(LOG_DEBUG, "request to stop process (%s)", command);

  ptr = processes;
  while ((ptr = g_slist_find_custom(ptr, command, compare_commands))) {
      dsme_process_t* proc = (dsme_process_t *) ptr->data;
      uid_t           oldeuid;

      /*
         Change euid to sender's UID.
         seteuid() can fail if dsme is not running as root.
         If dsme is running as root but seteuid() still fails most propably
         it means that someone tried to hack with capabilities.

         If kill() success with changed eiud then set process action to ONCE.

         And restore original EUID.
         */
      oldeuid = geteuid();
      if (seteuid(ucred->uid) == -1) {
          dsme_log(LOG_ERR, "seteuid(%d): %s", ucred->uid, strerror(errno));
      }

      if ((ucred->uid != 0) && (geteuid() == 0)) {
          dsme_log(LOG_ERR, "Someone tried to hack? (uid: %d)", ucred->uid);
      } else {
          if (kill(proc->pid, msg->signal) == -1) {
              dsme_log(LOG_ERR, 
                       "kill(%d, %d) => %s",
                       proc->pid, msg->signal,
                       strerror(errno));
          } else {
              proc->action = ONCE;
              returnmsg.killed = true;
              info             = "killed";
              dsme_log(LOG_DEBUG,
                       "process %d killed with signal %d",
                       proc->pid,
                       msg->signal);
          }
      }
      seteuid(oldeuid);

      ptr = g_slist_next(ptr);
  }

cleanup:
  endpoint_send_with_extra(client, &returnmsg, strlen(info) + 1, info);
}

static void send_reset_request()
{
	DSM_MSGTYPE_REBOOT_REQ msg = DSME_MSG_INIT(DSM_MSGTYPE_REBOOT_REQ);
	/* Send reset request here */
	dsme_log(LOG_CRIT, "Here we will request for sw reset");

	if (!lg_reboot_enabled) {
		dsme_log(LOG_CRIT, "Lifeguard reboots disabled from CAL");
		dsme_log(LOG_ERR, "The device is in unstable state, reboot manually!");
		return;
	}

	
	if (access(FILE_REBOOT_OVERRIDE, F_OK) != 0) { 
		broadcast_internally(&msg);
	} else {
		dsme_log(LOG_ERR, "The device not rebooted since %s is present", 
				FILE_REBOOT_OVERRIDE);
		dsme_log(LOG_ERR, "The device is in unstable state, reboot manually!");
	}
}
							
static void send_lifeguard_notice(u_int32_t type, const char* command)
{
  DSM_MSGTYPE_LG_NOTICE msg = DSME_MSG_INIT(DSM_MSGTYPE_LG_NOTICE);

  msg.notice_type = type;

  broadcast_with_extra(&msg, strlen(command) + 1, command);
  dsme_log(LOG_DEBUG, "Lifeguard notice message sent!");
}

DSME_HANDLER(DSM_MSGTYPE_PROCESS_EXITED, client, msg)
{
  GSList* ptr;
  GSList* next;

  for (ptr = processes; ptr; ptr = next) {
      dsme_process_t *proc;

      next = ptr->next;
      proc = (dsme_process_t *)ptr->data;

      if (proc->pid == msg->pid) {
          const char* reason;
          int         reason_value;

          int status = msg->status;
          if (WIFSIGNALED(status)) {
              reason       = "signal";
              reason_value = WTERMSIG(status);
          } else if (WIFEXITED(status)) {
              reason       = "return value";
              reason_value = WEXITSTATUS(status);
          } else {
              reason       = "status";
              reason_value = status;
          }

          switch (proc->action) {

            case ONCE:
              /* If exited process was type ONCE, dump it */
              dsme_log(LOG_CRIT,
                       "Process '%s' with pid %d exited with %s %d",
                       proc->command,
                       proc->pid,
                       reason,
                       reason_value);
              deleteprocess(proc);
              break;

            case RESPAWN: /* FALLTHRU */
            case RESPAWN_FAIL:
              if (proc->first_restart_time +
                  proc->restart_period > time(NULL))
              {
                  /* Within time - increase counter */
                  proc->restart_count++;
              } else {
                  /* If time period expired, set 
                     new start time and reset counter */
                  proc->first_restart_time = time(NULL);
                  proc->restart_count = 0;
              }

              /* Check if it hasn't respawned too fast */
              if (proc->restart_count >= proc->restart_limit) {
                  /* Try reset */
                  if (proc->uid == 0 || g_slist_find(uids, (void*)proc->uid)) {
                      if (proc->action == RESPAWN) {
                          dsme_log(LOG_CRIT,
                                   "Process '%s' with pid %d exited with %s %d; spawning too fast -> reset",
                                   proc->command,
                                   proc->pid,
                                   reason,
                                   reason_value);
                          update_reset_count(proc->command);
                          send_lifeguard_notice(DSM_LGNOTICE_RESET,
                                                proc->command);
                          send_reset_request(client);
                      } else {
                          /* RESPAWN_FAIL */
                          dsme_log(LOG_CRIT,
                                   "Process '%s' with pid %d exited with %s %d; spawning too fast, stop trying",
                                   proc->command,
                                   proc->pid,
                                   reason,
                                   reason_value);
                          send_lifeguard_notice(DSM_LGNOTICE_PROCESS_FAILED,
                                                proc->command);
                      }
                  } else {
                      dsme_log(LOG_CRIT,
                               "Non-root process '%s' with pid %d exited with %s %d; spawning too fast, remove it",
                               proc->command,
                               proc->pid,
                               reason,
                               reason_value);
                      send_lifeguard_notice(DSM_LGNOTICE_PROCESS_FAILED,
                                            proc->command);
                  }
                  /* delete the process since we are no longer restarting it */
                  deleteprocess(proc);
                  return;
              }

              /* restart proc */
              pid_t old_pid = proc->pid;
              proc->pid = spawn_proc(proc->command,
                                     proc->uid,
                                     proc->gid,
                                     proc->nice,
                                     proc->env);
              dsme_log(LOG_CRIT,
                       "Process '%s' with pid %d exited with %s %d and restarted with pid %d",
                       proc->command,
                       old_pid,
                       reason,
                       reason_value,
                       proc->pid);
              update_restart_count(proc->command);
              send_lifeguard_notice(DSM_LGNOTICE_PROCESS_RESTART,
                                    proc->command);
              break;

            case RESET:
              if (proc->uid == 0 || g_slist_find(uids, (void*)proc->uid)) {
                  dsme_log(LOG_CRIT,
                           "Process '%s' with pid %d exited with %s %d; reset due to RESET policy",
                           proc->command,
                           proc->pid,
                           reason,
                           reason_value);
                  update_reset_count(proc->command);
                  send_lifeguard_notice(DSM_LGNOTICE_RESET, proc->command);
                  send_reset_request(client);
              } else {
                  dsme_log(LOG_CRIT,
                           "Non-root process '%s' with pid %d exited with %s %d; remove it and ignore RESET policy",
                           proc->command,
                           proc->pid,
                           reason,
                           reason_value);
                  send_lifeguard_notice(DSM_LGNOTICE_PROCESS_FAILED,
                                        proc->command);
              }
              /* delete the process since we are not restarting it anymore */
              deleteprocess(proc);
              break;

            default:
              /* Should not be here, but...
                 remove entry if this happens */
              dsme_log(LOG_CRIT,
                       "process '%s' with pid %d exited with %s %d",
                       proc->command,
                       proc->pid,
                       reason,
                       reason_value);
              deleteprocess(proc);
              break;
          }
          break;
      }
  }
}

/**
 * This function selects the correct file to use for reset count stats and
 * calls increment_process_counter().
 * @param The name of the process that caused the reset
 */
static int update_reset_count(const char* process) {
	
	if (access(FILE_DSME_DIR, X_OK) == 0) {
		return increment_process_counter(FILE_DSME_LG_RESETS, process);
	} else {
		/* if we are on initfs, do nothing */
		dsme_log(LOG_ERR, "DSME stats dir not accessible, lifeguard stats not saved");
		return -1; 
	}
}

/**
 * This function selects the correct file to use for restart count stats and
 * calls increment_process_counter().
 * @param The name of the restarted process
 */
static int update_restart_count(const char* process) {

	if (access(FILE_DSME_DIR, X_OK) == 0) {
		return increment_process_counter(FILE_DSME_LG_RESTARTS, process);
	} else {
		/* if we are on initfs, do nothing */
		dsme_log(LOG_ERR, "DSME stats dir not accessible, lifeguard stats not saved");
		return -1; 
	}
}

/**
 * This function increments the process counter for lifeguard. If the preocess
 * it not in the file yet, it will be appended to the end. If the filesize is
 * bigger than 1kB, the first entry will be deleted.
 * @param statfilename Name of the Lifeguard stats filename
 * @param process The name of the process in the lifeguard
 */
static int increment_process_counter(const char* statfilename,
                                     const char* process)
{
	ssize_t     read_len    = 0;
	size_t      len         = 0;
	char*       line        = NULL;
	FILE*       statfile    = NULL;
	char*       tmpfilename = NULL;
	FILE*       tmpfile     = NULL;
	int         found       = 0;
	int         drop        = 0;
	struct stat filestats;

	tmpfilename = (char*)alloca(strlen(statfilename) + 5);
	if (!tmpfilename)
		return -1;

	tmpfilename = strcpy(tmpfilename, statfilename);
	tmpfilename = strcat(tmpfilename, ".tmp");

	tmpfile = fopen(tmpfilename, "w+");
	if (!tmpfile) {
		dsme_log(LOG_ERR, "Error opening tmpfile for Lifeguard stats");
		return -1;
	}
	dsme_log(LOG_DEBUG, "tmpfile: %s opened", tmpfilename);
	
	if (stat(statfilename, &filestats) == 0) {
		/* if the stat filesize is more than 1k, drop the first line */
		if (filestats.st_size > 1024) {
			dsme_log(LOG_INFO, "stats filesize > 1024, dropping the first line");
			drop = 1;
		}
	}
	
	statfile = fopen(statfilename, "r");
	if (statfile) {

		while ((read_len = getline(&line, &len, statfile)) != -1) {
			char * delim;
			int count = -1;
	
			if ((delim = strstr(line, DELIMETER)) == NULL) {
				dsme_log(LOG_ERR, "bad line found in lifeguard stats, dropping..");
				continue;
			}

			if (strncmp(line, process, delim-line) != 0) {
				if (!drop) {
					int len = strlen(line);
					if (line[len - 2] == '*') {
						line[len -2] = '\n';
						line[len -1] = '\0';
					}
					fprintf(tmpfile, "%s", line);
				} else {
					drop = 0;
				}
				continue;
			}

			char del[4];
			char *beginning = strndup(line, delim-line);
			if(!beginning) {
				dsme_log(LOG_CRIT, "strdup failed");
				exit(EXIT_FAILURE);
			}
			fprintf(tmpfile, "%s", beginning);
			sscanf(delim, "%s %i", del, &count);
			fprintf(tmpfile, "%s %i *\n", DELIMETER, ++count);
			found = 1;

			if (beginning)
				free(beginning);
		}
	}

	if (!found) {
		dsme_log(LOG_DEBUG, "process not found in lifeguard stats, appending..");
		fprintf(tmpfile, "%s : 1 *\n", process);
	}

	if (line)
		free(line);

	if(statfile)
		fclose(statfile);
	
	fclose(tmpfile);

	dsme_log(LOG_DEBUG, "renaming %s to %s", tmpfilename, statfilename);
	if(rename(tmpfilename, statfilename) == -1) {
		dsme_log(LOG_ERR, "rename failed");
	}

	return 0;
}

static int reboot_flag(void)
{
	void *vptr = NULL;
	unsigned long len = 0;
	int ret = 1;
	char *p;
	
	ret = cal_read_block(0, "r&d_mode", &vptr, &len, CAL_FLAG_USER);
	if (ret < 0) {
		dsme_log(LOG_ERR, "Error reading R&D mode flags, Lifeguard reboots enabled");
		return 1;
	}
	p = (char*)vptr;
	if (len >= 1 && *p) {
		dsme_log(LOG_DEBUG, "R&D mode enabled");

		if (len > 1) {
			if (strstr(p, "no-lifeguard-reset")) {
				ret = 0;
			} else {
				ret = 1;
			}
		} else {
			dsme_log(LOG_ERR, "No R&D mode flags found");
			ret = 1;
		}
	} else {
		ret = 1;
		dsme_log(LOG_DEBUG, "R&D mode disabled");
	}

	if (ret == 1)
		dsme_log(LOG_DEBUG, "Lifeguard resets enabled!");
	else
		dsme_log(LOG_DEBUG, "Lifeguard resets disabled!");
	
	free(vptr);
	return ret;
}


/**
 * @ingroup lifeguard
 * DSME messages handled by lifeguard-module
 * - DSM_MSGTYPE_PROCESS_START Request to start a new process.
 *   dsmemsg_start_process_t
 * - DSM_MSGTYPE_PROCESS_STOP Request to stop a process started by dsme.
 *   dsmemsg_stop_process_t
 * - DSM_MSGTYPE_PROCESS_EXITED Notification on exited process.
 * - DSM_MSGTYPE_STATE_CHANGE_IND Clear all monitoring information in
 *   case of reboot or shutdown
 */
module_fn_info_t message_handlers[] = {
    DSME_HANDLER_BINDING(DSM_MSGTYPE_PROCESS_START),
    DSME_HANDLER_BINDING(DSM_MSGTYPE_PROCESS_STOP),
    DSME_HANDLER_BINDING(DSM_MSGTYPE_PROCESS_EXITED),
    DSME_HANDLER_BINDING(DSM_MSGTYPE_STATE_CHANGE_IND),
    {0}
};

/**
 *  Module initialization function. 
 */
void module_init(module_t * handle)
{
	dsme_log(LOG_DEBUG, "liblifeguard.so loaded");

	read_priv_uids();
	lg_reboot_enabled = reboot_flag();
}

/**
 * Module cleanup function. Clears process list.
 */
void module_fini(void)
{
        spawn_shutdown();

        while (processes) {
		deleteprocess((dsme_process_t*)processes->data);
	}

        g_slist_free(uids);
        uids = 0;

	dsme_log(LOG_DEBUG, "liblifeguard.so unloaded");
}
