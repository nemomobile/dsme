/**
   @file malf.h

   DSME internal MALF messages
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

#ifndef DSME_MALF_H
#define DSME_MALF_H

#include <dsme/state.h>

typedef struct {
    DSMEMSG_PRIVATE_FIELDS
    int malf_reason;
} DSM_MSGTYPE_ENTER_MALF;

enum {
  DSME_MSG_ENUM(DSM_MSGTYPE_ENTER_MALF, 0x00000900),
};

/* reasons for DSME to enter to malf */
enum {
    DSME_MALF_HW_WD           = 0, /* malf: the device has been rebooted due to HW watchdog
                                            more than three times in a row within one hour */

    DSME_MALF_COMP_FAILURE    = 1, /* malf: upstart has rebooted the device because of software
                                            failure caused by the same component three times (or more) in a row */

    DSME_MALF_REBOOTLOOP      = 2  /* malf: upstart has rebooted the device because of software failure
                                            caused by any component five times (or more) */
};

const struct {
    const int  malf_id;
    const char *malf_type;    /* SOFTWARE, HARDWARE or SECURITY */
    const char *component;
    const char *reason;
} DSME_MALF[] = {
    {DSME_MALF_HW_WD,         "SOFTWARE",    "watchdog",   "caused too many reboots"},
    {DSME_MALF_COMP_FAILURE,  "SOFTWARE",    "%s",         "caused too many reboots"},
    {DSME_MALF_REBOOTLOOP,    "HARDWARE",    "unknown",    "too many reboots"},
    {0,0}
};

#endif
