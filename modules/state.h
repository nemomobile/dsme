/**
   @file state.h

   This file has defines for state module in DSME.
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

#ifndef DSME_STATE_H
#define DSME_STATE_H

#ifndef __cplusplus
#define _GNU_SOURCE
#endif

#include "dsme/messages.h"
#include <stdbool.h>

/**
   @ingroup state
   States in state-module.
*/

typedef enum {
#define DSME_STATE(STATE, VALUE) STATE = VALUE,
#include "state_states.h"
#undef  DSME_STATE
} dsme_state_t;

typedef struct {
    DSMEMSG_PRIVATE_FIELDS
    dsme_state_t state;
} DSM_MSGTYPE_STATE_CHANGE_IND;

typedef struct {
    DSMEMSG_PRIVATE_FIELDS
} DSM_MSGTYPE_SHUTDOWN_REQ;

typedef struct {
    DSMEMSG_PRIVATE_FIELDS
    dsme_state_t state;
} DSM_MSGTYPE_STATE_REQ_DENIED_IND;


typedef dsmemsg_generic_t DSM_MSGTYPE_STATE_QUERY;
typedef dsmemsg_generic_t DSM_MSGTYPE_SAVE_DATA_IND;
typedef dsmemsg_generic_t DSM_MSGTYPE_THERMAL_SHUTDOWN_IND;
typedef dsmemsg_generic_t DSM_MSGTYPE_REBOOT_REQ;
typedef dsmemsg_generic_t DSM_MSGTYPE_POWERUP_REQ;


enum {
    DSME_MSG_ENUM(DSM_MSGTYPE_STATE_CHANGE_IND,     0x00000301),
    DSME_MSG_ENUM(DSM_MSGTYPE_STATE_QUERY,          0x00000302),
    DSME_MSG_ENUM(DSM_MSGTYPE_SAVE_DATA_IND,        0x00000304),
    DSME_MSG_ENUM(DSM_MSGTYPE_POWERUP_REQ,          0x00000305),
    DSME_MSG_ENUM(DSM_MSGTYPE_SHUTDOWN_REQ,         0x00000306),
    DSME_MSG_ENUM(DSM_MSGTYPE_REBOOT_REQ,           0x00000308),
    DSME_MSG_ENUM(DSM_MSGTYPE_STATE_REQ_DENIED_IND, 0x00000309),
    DSME_MSG_ENUM(DSM_MSGTYPE_THERMAL_SHUTDOWN_IND, 0x00000310),
};


typedef struct {
  DSMEMSG_PRIVATE_FIELDS
  bool alarm_set;
} DSM_MSGTYPE_SET_ALARM_STATE;

typedef struct {
  DSMEMSG_PRIVATE_FIELDS
  bool connected;
} DSM_MSGTYPE_SET_CHARGER_STATE;

typedef struct {
  DSMEMSG_PRIVATE_FIELDS
  bool overheated;
} DSM_MSGTYPE_SET_THERMAL_STATE;

typedef struct {
  DSMEMSG_PRIVATE_FIELDS
  bool ongoing;
} DSM_MSGTYPE_SET_EMERGENCY_CALL_STATE;

typedef struct {
  DSMEMSG_PRIVATE_FIELDS
  bool empty;
} DSM_MSGTYPE_SET_BATTERY_STATE;

enum {
  DSME_MSG_ENUM(DSM_MSGTYPE_SET_ALARM_STATE,          0x00000307),
  DSME_MSG_ENUM(DSM_MSGTYPE_SET_CHARGER_STATE,        0x00000311),
  DSME_MSG_ENUM(DSM_MSGTYPE_SET_THERMAL_STATE,        0x00000312),
  DSME_MSG_ENUM(DSM_MSGTYPE_SET_EMERGENCY_CALL_STATE, 0x00000313), /* D. Duck */
  DSME_MSG_ENUM(DSM_MSGTYPE_SET_BATTERY_STATE,        0x00000314),
};

#endif
