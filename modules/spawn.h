/**
   @file spawn.h

   Prototypes for public interfaces in spawn.c
   <p>
   Copyright (C) 2004-2009 Nokia Corporation.

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

#ifndef DSME_SPAWN_H
#define DSME_SPAWN_H

#include "dsme/messages.h"
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
   Specific message type that is used to notify about exited processes.
   @ingroup message_if
*/
enum {
    DSME_MSG_ENUM(DSM_MSGTYPE_PROCESS_EXITED, 0x00000400),
};

typedef struct {
  DSMEMSG_PRIVATE_FIELDS
  pid_t pid;
  int   status;
} DSM_MSGTYPE_PROCESS_EXITED;


/**
   Executes a command as new process

   If command has redirections or some other services provided by
   shell, the new process will be spawned through @c /bin/sh.

   @param cmdline  Command line for the new process.
   @param uid      User id for new process
   @param gid      Group id for new process
   @param nice_val Nice value of the new process
   @param env      Pointer to anvironment array for new process
   @return Process id of started process.
*/
pid_t spawn_proc(const char* cmdline,
                 uid_t       uid,
                 gid_t       gid,
                 int         nice_val,
                 int         oom_adj,
                 char*       env[]);

void spawn_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
