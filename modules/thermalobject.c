/**
   @file thermalmanager.c

   This file implements part of the device thermal management policy
   by providing the current thermal state for interested sw components.
   <p>
   Copyright (C) 2009-2010 Nokia Corporation
   Copyright (C) 2013-2015 Jolla Oy.

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

#define _GNU_SOURCE

#include "thermalmanager.h"

#include <iphbd/iphb_internal.h>

#include "dbusproxy.h"
#include "dsme_dbus.h"

#include "../include/dsme/modules.h"
#include "../include/dsme/modulebase.h"
#include "../include/dsme/logging.h"
#include "heartbeat.h"

#include <dsme/state.h>
#include <dsme/thermalmanager_dbus_if.h>

#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ========================================================================= *
 * FORWARD_DECLARATIONS
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * DIAGNOSTIC_LOGGING
 * ------------------------------------------------------------------------- */

/** Prefix string to use for all logging from this module */
#define PFIX "thermal object: "

/** Whether to log thermal status changes to a separate file */
#define DSME_THERMAL_LOGGING 0

/* ------------------------------------------------------------------------- *
 * MISC_UTIL
 * ------------------------------------------------------------------------- */

static time_t to_util_monotime(void);

/* ------------------------------------------------------------------------- *
 * THERMAL_OBJECT
 * ------------------------------------------------------------------------- */

/** Thermal object state data */
struct thermal_object_t
{
    /** Currently accepted stable thermal status */
    THERMAL_STATUS               to_status_curr;

    /** Latest status as reported by the sensor */
    THERMAL_STATUS               to_status_next;

    /** Latest temperature reading */
    int                          to_temperature;

    /** Time stamp when status change started */
    time_t                       to_status_change_started;

    /** Temperature request has been issued to sensor */
    bool                         to_request_pending;

    /** Sensor backend functions */
    const thermal_sensor_vtab_t *to_sensor_vtab;

    /** Sensor backend data */
    void                        *to_sensor_data;
};

thermal_object_t *thermal_object_create                (const thermal_sensor_vtab_t *vtab, void *data);
void              thermal_object_delete                (thermal_object_t *self);

const char       *thermal_object_get_name              (const thermal_object_t *self);
bool              thermal_object_has_name              (const thermal_object_t *self, const char *name);
bool              thermal_object_has_name_like         (const thermal_object_t *self, const char *name);

const char       *thermal_object_get_depends_on           (const thermal_object_t *self);

THERMAL_STATUS    thermal_object_get_status            (const thermal_object_t *self);
int               thermal_object_get_temperature       (const thermal_object_t *self);
bool              thermal_object_get_poll_delay        (thermal_object_t *self, int *mintime, int *maxtime);
bool              thermal_object_status_in_transition  (const thermal_object_t *self);

#if DSME_THERMAL_LOGGING
static void       thermal_object_log_status            (const thermal_object_t *self);
#endif

bool              thermal_object_has_valid_sensor_vtab (const thermal_object_t *self);
bool              thermal_object_has_sensor_vtab       (const thermal_object_t *self, const thermal_sensor_vtab_t *vtab);
void             *thermal_object_get_sensor_data       (const thermal_object_t *self);
bool              thermal_object_get_sensor_status     (thermal_object_t *self, THERMAL_STATUS *status, int *temperature);

void              thermal_object_request_update        (thermal_object_t *self);
bool              thermal_object_update_is_pending     (const thermal_object_t *self);
void              thermal_object_handle_update         (thermal_object_t *self);
void              thermal_object_cancel_update         (thermal_object_t *self);
bool              thermal_object_read_sensor           (thermal_object_t *self);

/* ========================================================================= *
 * MISC_UTIL
 * ========================================================================= */

/** Helper for getting monotonic timestamp
 *
 * Uses CLOCK_BOOTTIME as it is both monotonic and accounts also
 * time spent in suspend.
 *
 * @returns seconds since unspecified epoch
 */
static time_t
to_util_monotime(void)
{
    struct timespec ts = { 0, 0 };
    clock_gettime(CLOCK_BOOTTIME, &ts);
    return ts.tv_sec;
}

/* ========================================================================= *
 * THERMAL_OBJECT
 * ========================================================================= */

/** Create thermal object
 *
 * @param vtab  functions sensor backend must implement
 * @param data  data that the sensor backend needs to have available
 *
 * @return pointer to thermal object
 */
thermal_object_t *
thermal_object_create(const thermal_sensor_vtab_t *vtab, void *data)
{
    thermal_object_t *self = calloc(1, sizeof *self);

    self->to_temperature = INVALID_TEMPERATURE;
    self->to_status_curr = THERMAL_STATUS_NORMAL;
    self->to_status_next = THERMAL_STATUS_NORMAL;

    self->to_status_change_started = 0;
    self->to_request_pending = false;

    self->to_sensor_vtab = vtab;
    self->to_sensor_data = data;

    dsme_log(LOG_DEBUG, PFIX"%s: created", thermal_object_get_name(self));

    return self;
}

/** Delete thermal object
 *
 * @param self  thermal object pointer, or NULL
 */
void
thermal_object_delete(thermal_object_t *self)
{
    if( !self )
        goto EXIT;

    thermal_manager_unregister_object(self);

    dsme_log(LOG_DEBUG, PFIX"%s: deleted", thermal_object_get_name(self));

    if( thermal_object_has_valid_sensor_vtab(self) )
        self->to_sensor_vtab->tsv_delete_cb(self);

    self->to_sensor_vtab = 0;
    self->to_sensor_data = 0;

    free(self);

EXIT:
    return;
}

/** Get name of thermal object
 *
 * @param self  thermal object pointer, or NULL
 *
 * @return object name, or "invalid" in case of errors
 */
const char *
thermal_object_get_name(const thermal_object_t *self)
{
    const char *res = "invalid";
    if( thermal_object_has_valid_sensor_vtab(self) )
        res = self->to_sensor_vtab->tsv_get_name_cb(self);
    return res ?: "unnamed";
}

/** Check if thermal object matches given name exactly
 *
 * @param self  thermal object pointer
 * @param name  required name
 *
 * @return true if thermal object name matches, false otherwise
 */
bool
thermal_object_has_name(const thermal_object_t *self, const char *name)
{
    const char *have = thermal_object_get_name(self);
    return have && name && !strcmp(have, name);
}

/** Check if thermal object matches given name prefix
 *
 * @param self  thermal object pointer
 * @param name  required name
 *
 * @return true if thermal object name matches, false otherwise
 */
bool
thermal_object_has_name_like(const thermal_object_t *self, const char *name)
{
    bool matches = false;

    if( !name )
        goto EXIT;

    const char *have = thermal_object_get_name(self);

    size_t length = strlen(name);

    if( strncmp(have, name, length) )
        goto EXIT;

    // "core" matches "core" or "core0", "core1", "core:foo" etc

    int ch = have[length];

    matches = (ch == 0) || (ch == ':') || ('0' <= ch && ch <= '9');

EXIT:
    return matches;
}

/** Get name of sensor required by thermal object
 *
 * If the thermal object is a meta-sensor based on
 * temperature value from some other sensor, this
 * function will return the name of the sensor
 * that is required for evaluating the status of
 * the thermal object.
 *
 * @param self  thermal object pointer
 *
 * @return sensor name, or NULL
 */
const char *
thermal_object_get_depends_on(const thermal_object_t *self)
{
    const char *res = NULL;
    if( thermal_object_has_valid_sensor_vtab(self) )
        res = self->to_sensor_vtab->tsv_get_depends_on_cb(self);
    return res;
}

/** Get the currently cache stable status of thermal object
 *
 * @param self  thermal object pointer
 *
 * @return thermal status, or THERMAL_STATUS_INVALID
 */
THERMAL_STATUS
thermal_object_get_status(const thermal_object_t *self)
{
    return self ? self->to_status_curr : THERMAL_STATUS_INVALID;
}

/** Get the currently temperature value of thermal object
 *
 * @param self  thermal object pointer
 *
 * @return temperature, or INVALID_TEMPERATURE
 */
int
thermal_object_get_temperature(const thermal_object_t *self)
{
    return self ? self->to_temperature : INVALID_TEMPERATURE;
}

/** Get the polling delay for thermal object
 *
 * The polling delay is defined by the sensor backend and depend
 * on the sensor backend status.
 *
 * @param self  thermal object pointer
 *
 * @return true if mintime/maxtime was filled in, false otherwise
 */
bool
thermal_object_get_poll_delay(thermal_object_t *self,
                              int *mintime, int *maxtime)
{
    bool ack = false;

    if( !thermal_object_has_valid_sensor_vtab(self) )
        goto EXIT;

    ack = self->to_sensor_vtab->tsv_get_poll_delay_cb(self, mintime, maxtime);

EXIT:
    return ack;
}

/** Check if the thermal object is about to change status
 *
 * @param self  thermal object pointer
 *
 * @return true if thermal status is unstable, false otherwise
 */
bool
thermal_object_status_in_transition(const thermal_object_t *self)
{
    return self && self->to_status_change_started > 0;
}

/** [Optional] Helper for logging thermal object status changes
 *
 * @param self  thermal object pointer
 */
#if DSME_THERMAL_LOGGING
static void
thermal_object_log_status(const thermal_object_t *self)
{
    static const char  log_path[] = "/var/lib/dsme/thermal.log";
    static FILE       *log_file   = 0;
    static bool        did_open   = false;

    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);

    int now = t.tv_sec;
    static int start_time = 0;

    if( !start_time ) {
        start_time = now;
    }
    int temperature = thermal_object_get_temperature(self);

    dsme_log(LOG_DEBUG, PFIX"%s: %dC %s", thermal_object_get_name(self),
             temperature, thermal_status_name(self->to_status_curr));

    if( !did_open ) {
        did_open = true;

        if( !(log_file = fopen(log_path, "a")) ) {
            dsme_log(LOG_ERR, PFIX"%s: Error opening thermal log: %m",
                     log_path);
        }
    }

    if( log_file ) {
        fprintf(log_file, "%d %s %d C %s\n",
                now - start_time,
                thermal_object_get_name(self),
                temperature,
                thermal_status_name(self->to_status_curr));
        fflush(log_file);
    }
}
#endif

/** Check if thermal object has required sensor backend information
 *
 * @param self  thermal object pointer
 *
 * @return true if sensor backend is fully defined, false otherwise
 */
bool
thermal_object_has_valid_sensor_vtab(const thermal_object_t *self)
{
    return self && self->to_sensor_vtab && self->to_sensor_data;
}

/** Check if thermal object is using the given sensor backend
 *
 * @param self  thermal object pointer
 *
 * @return true if sensor backend matches, false otherwise
 */
bool
thermal_object_has_sensor_vtab(const thermal_object_t *self,
                               const thermal_sensor_vtab_t *vtab)
{
    return self && self->to_sensor_vtab == vtab;
}

/** Get thermal object sensor backend data
 *
 * @param self  thermal object pointer
 *
 * @return sensor backend data pointer, or NULL
 */
void *
thermal_object_get_sensor_data(const thermal_object_t *self)
{
    return self ? self->to_sensor_data : 0;
}

/** Get thermal object sensor backend status
 *
 * @param self         thermal object pointer
 * @param status       where to store status enum
 * @param temperature  where to store temperature
 *
 * @return true if status and temperature were set, false otherwise
 */
bool
thermal_object_get_sensor_status(thermal_object_t *self,
                                  THERMAL_STATUS *status, int *temperature)
{
    bool ack = false;

    if( thermal_object_has_valid_sensor_vtab(self) )
        ack = self->to_sensor_vtab->tsv_get_status_cb(self, status,
                                                     temperature);
    return ack;
}

/** Initiate temperature sensor reading
 *
 * @param self         thermal object pointer
 *
 * If return value is true, a call to thermal_object_handle_update()
 * has already been made / or will be made later on when temperature
 * data is available.
 */
bool
thermal_object_read_sensor(thermal_object_t *self)
{
    bool ack = false;

    if( thermal_object_has_valid_sensor_vtab(self) )
        ack = self->to_sensor_vtab->tsv_read_sensor_cb(self);

    return ack;
}

/** Schedule temperature sensor re-evaluation
 *
 * @param self         thermal object pointer
 *
 * Every call to thermal_object_request_update() is followed by
 * a call to thermal_object_handle_update() function - either when
 * the updated temperature value is available or when it becomes
 * known that the value can't be read.
 *
 * Exception: Multiple overlapping requests are ignored.
 */
void
thermal_object_request_update(thermal_object_t *self)
{
    bool success = true;

    /* Skip if request has already been passed to lower level
     */
    if( self->to_request_pending ) {
        dsme_log(LOG_DEBUG, PFIX"%s: still waiting for temperature",
                 thermal_object_get_name(self));
        goto EXIT;
    }

    /* If the temperature measurement can be taken immediately, the flag
     * can end up being inspected before the control returns back to
     * this function.
     */
    self->to_request_pending = true, success = false;

    dsme_log(LOG_DEBUG, PFIX"%s: requesting temperature",
             thermal_object_get_name(self));

    const char *depends_on = thermal_object_get_depends_on(self);

    if( depends_on ) {
        /* We are dealing with a meta sensor, handle
         * the request via thermal manager.
         */
        success = thermal_manager_request_sensor_update(depends_on);
    }
    else {
        /* Initiate sensor read at sensor backend. When done a
         * call to thermal_object_handle_update() is made.
         */
        success = thermal_object_read_sensor(self);
    }

EXIT:

    /* If the lower level request failed, notify upper level
     * logic immediately */

    if( !success ) {
        dsme_log(LOG_ERR, PFIX"%s: error requesting temperature",
                 thermal_object_get_name(self));
        thermal_object_handle_update(self);
    }

    return;
}

/** Check if thermal object is waiting for sensor backend status query
 *
 * @param self         thermal object pointer
 *
 * @return true if status is not yet available, false otherwise
 */
bool
thermal_object_update_is_pending(const thermal_object_t *self)
{
    return self ? self->to_request_pending : false;
}

/** Re-evaluate thermal object status
 *
 * Called by sensor backend when reading of temperature data is
 * finished.
 *
 * @param self         thermal object pointer
 */
void
thermal_object_handle_update(thermal_object_t *self)
{
    THERMAL_STATUS    status      = THERMAL_STATUS_INVALID;
    int               temperature = INVALID_TEMPERATURE;
    bool              notify      = false;

    /* Upper level must be notified when pendig request
     * is finished */

    /* Check if we are expecting a reply */
    if( !self->to_request_pending )
        goto EXIT;

    self->to_request_pending = false, notify = true;

    /* Get sensor status from sensor backend */
    if( !thermal_object_get_sensor_status(self, &status, &temperature) ) {
        dsme_log(LOG_DEBUG, PFIX"%s: temperature request failed",
                 thermal_object_get_name(self));
        goto EXIT;
    }

    /* We require that temp readings are C degrees integers
     * and we drop obviously wrong values
     */
    if( temperature < IGNORE_TEMP_BELOW || temperature > IGNORE_TEMP_ABOVE ) {
        dsme_log(LOG_WARNING, PFIX"%s: invalid temperature reading: %dC",
                 thermal_object_get_name(self),
                 temperature);
        goto EXIT;
    }

    dsme_log(LOG_DEBUG, PFIX"%s: temperature=%d status=%s",
             thermal_object_get_name(self),
             temperature,
             thermal_status_repr(status));

    /* The cached temperature value is updated even if the
     * effective status does not change
     */
    self->to_temperature = temperature;

    /* If we are in or arrive back to stable status,
     * clear the in-transition flags
     */
    if( self->to_status_curr == status ) {
        if( self->to_status_next != status )
            dsme_log(LOG_NOTICE, PFIX"%s: transition to status=%s %s at temperature=%d",
                     thermal_object_get_name(self),
                     thermal_status_repr(self->to_status_next),
                     "canceled",
                     temperature);
        self->to_status_next = status;
        self->to_status_change_started = 0;
        goto EXIT;
    }

    /* Thermal object status has changed, but it can be because of bad reading.
     * Before accepting new status, make sure it is not a glitch.
     * Use more frequent polling frequency and accept the new state if
     * it stays effective over THERMAL_STATUS_TRANSITION_DELAY seconds.
     */

    time_t now = to_util_monotime();

    if( self->to_status_next != status ) {
        self->to_status_next = status;
        self->to_status_change_started = now;

        dsme_log(LOG_NOTICE, PFIX"%s: transition to status=%s %s at temperature=%d",
                 thermal_object_get_name(self),
                 thermal_status_repr(self->to_status_next),
                 "started",
                 temperature);
        goto EXIT;
    }

    time_t limit = (self->to_status_change_started +
                    THERMAL_STATUS_TRANSITION_DELAY);

    if( now <= limit ) {
            dsme_log(LOG_NOTICE, PFIX"%s: transition to status=%s %s at temperature=%d",
                     thermal_object_get_name(self),
                     thermal_status_repr(self->to_status_next),
                     "pending",
                     temperature);
        goto EXIT;
    }

    /* The new status stayed active long enough, better believe it */

    dsme_log(LOG_NOTICE, PFIX"%s: transition to status=%s %s at temperature=%d",
             thermal_object_get_name(self),
             thermal_status_repr(self->to_status_next),
             "accepted",
             temperature);

    self->to_status_curr = status;
    self->to_status_next = status;
    self->to_status_change_started = 0;
    self->to_temperature = temperature;

#if DSME_THERMAL_LOGGING
    thermal_object_log_status(self);
#endif

EXIT:

    if( notify ) {
        /* Update the thermal object itself */
        thermal_manager_handle_object_update(self);

        /* And objects that depend on it */
        thermal_manager_handle_sensor_update(self);
    }

    return;
}

/** Remove thermal object is waiting for update state
 *
 * @param self         thermal object pointer
 */
void
thermal_object_cancel_update(thermal_object_t *self)
{
    if( self )
        self->to_request_pending = false;
}
