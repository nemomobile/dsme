/**
   @file malf.c

   DSME MALF (malfunction) state handling
   <p>
   Copyright (C) 2009-2010 Nokia Corporation.

   @author Matias Muhonen <ext-matias.muhonen@nokia.com>

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

#include "malf.h"
#include "dsme/modules.h"
#include "dsme/logging.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

static void enter_malf(void);

static void enter_malf(void)
{
    // TODO
}

DSME_HANDLER(DSM_MSGTYPE_ENTER_MALF, conn, msg)
{
    enter_malf();
}


module_fn_info_t message_handlers[] = {
    DSME_HANDLER_BINDING(DSM_MSGTYPE_ENTER_MALF),
    { 0 }
};

void module_init(module_t* module)
{
    dsme_log(LOG_DEBUG, "malf.so loaded");
}

void module_fini(void)
{
    dsme_log(LOG_DEBUG, "malf.so unloaded");
}
