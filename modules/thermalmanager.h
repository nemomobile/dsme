/**
   @file thermalmanager.h

   DSME thermal object abstraction
   <p>
   Copyright (C) 2009-2010 Nokia Corporation.
   Copyright (C) 2014-2015 Jolla Oy.

   @author Semi Malinen <semi.malinen@nokia.com>
   @author Pekka Lundstrom <pekka.lundstrom@jollamobile.com>
   @author Simo Piiroinen <simo.piiroinen@jollamobile.com>

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
#ifndef THERMALMANAGER_H_
#define THERMALMANAGER_H_

#include <stdbool.h>

/* ------------------------------------------------------------------------- *
 * TEMPERATURE
 * ------------------------------------------------------------------------- */

/** Value to use when temperature is not known */
#define INVALID_TEMPERATURE -9999

/** Low bound for acceptable temperature values */
#define IGNORE_TEMP_BELOW -50

/** High bound for acceptable temperature values */
#define IGNORE_TEMP_ABOVE 200

/* ------------------------------------------------------------------------- *
 * THERMAL_STATUS
 * ------------------------------------------------------------------------- */

/** Thermal status */
typedef enum {
    THERMAL_STATUS_LOW,
    THERMAL_STATUS_NORMAL,
    THERMAL_STATUS_WARNING,
    THERMAL_STATUS_ALERT,
    THERMAL_STATUS_FATAL,
    THERMAL_STATUS_INVALID,

    THERMAL_STATUS_COUNT,
} THERMAL_STATUS;

const char *thermal_status_name(THERMAL_STATUS status);
const char *thermal_status_repr(THERMAL_STATUS status);

/** Default poll delay [s]
 *
 * Used as fallback if sensor specific configuration is not available
 */
#define THERMAL_STATUS_POLL_DELAY_DEFAULT_MINIMUM  60
#define THERMAL_STATUS_POLL_DELAY_DEFAULT_MAXIMUM 120

/** Shorter high temperature poll delay [s]
 *
 * Used as fallback if sensor specific configuration is not available
 */
#define THERMAL_STATUS_POLL_ALERT_TRANSITION_MINIMUM  5
#define THERMAL_STATUS_POLL_ALERT_TRANSITION_MAXIMUM  10

/** Poll delay to use during thermal status transition [s]
 *
 * Overrides sensor specific configuration
 */
#define THERMAL_STATUS_POLL_DELAY_TRANSITION_MINIMUM  3
#define THERMAL_STATUS_POLL_DELAY_TRANSITION_MAXIMUM  5

/** Minimum stable time before new thermal status is accepted [s]
 *
 * When status transition is detected, we want at least 3 more
 * samples that agree with the change before accepting it.
 *
 * Set the time limit at 2.5 * maximum poll delay.
 */
#define THERMAL_STATUS_TRANSITION_DELAY \
     (THERMAL_STATUS_POLL_DELAY_TRANSITION_MAXIMUM * 5 / 2)

/* ------------------------------------------------------------------------- *
 * THERMAL_SENSOR_VTAB
 * ------------------------------------------------------------------------- */

typedef struct thermal_sensor_vtab_t thermal_sensor_vtab_t;
typedef struct thermal_object_t      thermal_object_t;

/** Hook functions thermal sensor needs to provide for thermal object */
struct thermal_sensor_vtab_t
{
    /** Hook used by thermal_object_delete() */
    void        (*tsv_delete_cb)(thermal_object_t *);

    /** Hook used by thermal_object_get_name() */
    const char *(*tsv_get_name_cb)(const thermal_object_t *);

    /** Hook used by thermal_object_get_depends_on() */
    const char *(*tsv_get_depends_on_cb)(const thermal_object_t *);

    /** Hook required by thermal_object_get_sensor_status() */
    bool        (*tsv_get_status_cb)(const thermal_object_t *, THERMAL_STATUS *, int *);

    /** Hook required by thermal_object_get_poll_delay() */
    bool        (*tsv_get_poll_delay_cb)(const thermal_object_t *, int *, int *);

    /** Hook required by thermal_object_read_sensor() */
    bool        (*tsv_read_sensor_cb)(thermal_object_t *);

};

/* ------------------------------------------------------------------------- *
 * THERMAL_OBJECT
 * ------------------------------------------------------------------------- */

thermal_object_t *thermal_object_create(const thermal_sensor_vtab_t *vtab, void *data);
void              thermal_object_delete(thermal_object_t *self);

const char       *thermal_object_get_name(const thermal_object_t *self);
bool              thermal_object_has_name(const thermal_object_t *self, const char *name);
bool              thermal_object_has_name_like(const thermal_object_t *self, const char *name);

THERMAL_STATUS    thermal_object_get_status(const thermal_object_t *self);
int               thermal_object_get_temperature(const thermal_object_t *self);

bool              thermal_object_update_is_pending(const thermal_object_t *self);
void              thermal_object_cancel_update(thermal_object_t *self);

bool              thermal_object_status_in_transition(const thermal_object_t *self);
void             *thermal_object_get_sensor_data(const thermal_object_t *self);
bool              thermal_object_has_sensor_vtab(const thermal_object_t *self, const thermal_sensor_vtab_t *vtab);

bool              thermal_object_has_valid_sensor_vtab(const thermal_object_t *self);

bool              thermal_object_get_poll_delay(thermal_object_t *self, int *mintime, int *maxtime);
bool              thermal_object_get_sensor_status(thermal_object_t *self, THERMAL_STATUS *status, int *temperature);
bool              thermal_object_read_sensor(thermal_object_t *self);

void              thermal_object_handle_update(thermal_object_t *self);
void              thermal_object_request_update(thermal_object_t *self);
const char       *thermal_object_get_depends_on(const thermal_object_t *self);

/* ------------------------------------------------------------------------- *
 * THERMAL_MANAGER
 * ------------------------------------------------------------------------- */

void thermal_manager_register_object(thermal_object_t* thermal_object);
void thermal_manager_unregister_object(thermal_object_t* thermal_object);
bool thermal_manager_object_is_registered(thermal_object_t* object);
bool thermal_manager_get_sensor_status(const char *sensor, THERMAL_STATUS *status, int *temperature);

bool thermal_manager_request_sensor_update(const char *sensor_name);
void thermal_manager_handle_sensor_update(const thermal_object_t *changed_object);
void thermal_manager_handle_object_update(thermal_object_t *changed_object);

#endif /* THERMALMANAGER_H_ */
