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

/*
 * An example command line to obtain thermal state over D-Bus:
 * $ dbus-send --system --print-reply --dest=com.nokia.thermalmanager /com/nokia/thermalmanager com.nokia.thermalmanager.get_thermal_state
 *
 * An example command line to obtain surface temperature estimate over D-Bus:
 * $ dbus-send --system --print-reply --dest=com.nokia.thermalmanager /com/nokia/thermalmanager com.nokia.thermalmanager.estimate_surface_temperature
 *
 * An example command line to obtain core temperature over D-Bus:
 * $ dbus-send --system --print-reply --dest=com.nokia.thermalmanager /com/nokia/thermalmanager com.nokia.thermalmanager.core_temperature
 *
 * An example command line to obtain battery temperature over D-Bus:
 * $ dbus-send --system --print-reply --dest=com.nokia.thermalmanager /com/nokia/thermalmanager com.nokia.thermalmanager.battery_temperature
 *
 * An example command line to obtain named sensor temperature over D-Bus:
 * $ dbus-send --system --print-reply --dest=com.nokia.thermalmanager /com/nokia/thermalmanager com.nokia.thermalmanager.sensor_temperature string:core
 */

/* ========================================================================= *
 *
 * Thermal manager:
 * - maintains a set of registered thermal objects
 * - polls status of each thermal object periodically
 * - evaluates overall device thermal status
 * - broadcasts thermal status changes within dsme and over D-Bus
 * - handles thermal sensor related D-Bus queries
 *
 * Thermal sensor:
 * - handles the actual sensor reading
 * - defines the thermal limits and associated polling delays
 * - hw specific sensors can be made available as plugins
 *
 * Thermal object:
 * - acts as glue layer allowing communication between thermal
 *   manager and possibly hw specific sensor logic
 *
 * +-------------------------+
 * |                         |
 * |  THERMAL_MANAGER        |
 * |                         |
 * +-------------------------+
 *           ^ 1
 *           |
 *           |
 *           v N
 * +-------------------------+
 * |                         |
 * |  THERMAL_OBJECT         |
 * |                 +-------|
 * |-----------------| VTAB  |
 * |                 +-------|
 * |  THERMAL_SENSOR         |
 * |                         |
 * +-------------------------+
 *
 * The included-by-default generic thermal sensor plugin:
 * - can read file based sensors (in /sys and /proc file systems)
 * - allows enabling/disabling sensors on dsme startup/exit
 * - allows configuring meta-sensors which take temperature
 *   value from some other sensor, apply optional offset and
 *   can define separate thermal limits for the resulting
 *   values (e.g. surface_temp = battery_temp - 3 etc).
 *
 * HW specific plugins can/must be added to deal with thermal
 * sensors that can't be accessed via filesystem (e.g. old nokia
 * devices where battery temperature is accessible via bme ipc only).
 * The sensor update logic in thermal manager has been implemented
 * to expect asynchronous sensor access to facilitate such indirect
 * sensor polling.
 *
 * Simplified graph of sensor poll duty loop:
 *
 *     DSME_STARTUP
 *           |
 *           v
 *     thermal_sensor
 *           |
 *           v
 *     thermal_manager_register_object
 *           |
 *           v
 *     thermal_manager_request_object_update <---------.
 *           |                                         |
 *           v                                         |
 *     thermal_object_request_update                   |
 *           |                                         |
 *           v                                         |
 *     thermal_sensor                                  |
 *           |                                         |
 *           Z (assumed: sensor access is async)       Z iphb_timer
 *           |                                         |
 *           v                                         |
 *     thermal_object_handle_update                    |
 *           |                                         |
 *           v                                         |
 *     thermal_manager_handle_object_update            |
 *           |                 |                       |
 *           |                 v                       |
 *           |              thermal_manager_schedule_object_poll
 *           v
 *     thermal_manager_broadcast_status
 *         |                   |
 *         Z DBUS              Z DSME
 *         |                   |
 *         v                   v
 *     UI_NOTIFICATION     SHUTDOWN_POLICY
 *
 * More descriptive diagram can be generated from thermalmanager.dot.
 *
 * ========================================================================= */

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

/** Prefix to use for all logging from this module */
#define PFIX "thermalmanager: "

/* ------------------------------------------------------------------------- *
 * THERMAL_STATUS
 * ------------------------------------------------------------------------- */

const char *thermal_status_name (THERMAL_STATUS status);
const char *thermal_status_repr (THERMAL_STATUS status);

/* ------------------------------------------------------------------------- *
 * THERMAL_MANAGER
 * ------------------------------------------------------------------------- */

void        thermal_manager_register_object            (thermal_object_t *thermal_object);
void        thermal_manager_unregister_object          (thermal_object_t *thermal_object);
bool        thermal_manager_object_is_registered       (thermal_object_t *thermal_object);
static void thermal_manager_request_object_update      (thermal_object_t *thermal_object);
void        thermal_manager_handle_object_update       (thermal_object_t *changed_object);
static void thermal_manager_schedule_object_poll       (thermal_object_t *thermal_object);

bool        thermal_manager_get_sensor_status          (const char *sensor_name, THERMAL_STATUS *status, int *temperature);
bool        thermal_manager_request_sensor_update      (const char *sensor_name);
void        thermal_manager_handle_sensor_update       (const thermal_object_t *thermal_object);
static bool thermal_manager_have_pending_sensor_update (const char *sensor_name);

static void thermal_manager_broadcast_status           (THERMAL_STATUS status, thermal_object_t *changed_object);
static void thermal_manager_broadcast_status_dsme      (THERMAL_STATUS status, int temperature, const char *sensor_name);
static void thermal_manager_broadcast_status_dbus      (THERMAL_STATUS status);

/* ------------------------------------------------------------------------- *
 * DBUS_HANDLERS
 * ------------------------------------------------------------------------- */

static void thermal_manager_get_thermal_state_cb       (const DsmeDbusMessage *request, DsmeDbusMessage **reply);

static void thermal_manager_handle_temperature_query   (const DsmeDbusMessage *req, const char *sensor_name, DsmeDbusMessage **rsp);
static void thermal_manager_get_surface_temperature_cb (const DsmeDbusMessage *req, DsmeDbusMessage **rsp);
static void thermal_manager_get_core_temperature_cb    (const DsmeDbusMessage *req, DsmeDbusMessage **rsp);
static void thermal_manager_get_battery_temperature_cb (const DsmeDbusMessage *req, DsmeDbusMessage **rsp);
static void thermal_manager_get_sensor_temperature_cb  (const DsmeDbusMessage *req, DsmeDbusMessage **rsp);

/* ------------------------------------------------------------------------- *
 * MODULE_GLUE
 * ------------------------------------------------------------------------- */

void module_init (module_t *handle);
void module_fini (void);

/* ========================================================================= *
 * MODULE_DATA
 * ========================================================================= */

/** DSME module handle for this module */
static module_t *this_module = 0;

/** List of registered thermal objects */
static GSList *thermal_objects = 0;

/** Currently accepted device thermal state */
static THERMAL_STATUS current_status = THERMAL_STATUS_NORMAL;

/** Flag for: D-Bus method handlers have been registered */
static bool dbus_methods_bound = false;

/* ========================================================================= *
 * THERMAL_STATUS
 * ========================================================================= */

/** Convert thermal status to name used in D-Bus signaling
 *
 * @param  status thermal status
 *
 * @return string expected by dbus peers
 */
const char *
thermal_status_name(THERMAL_STATUS status)
{
    /* Note: Any deviation from strings defined in thermalmanager_dbus_if
     *       from libdsme constitutes a possible D-Bus API break and should
     *       be avoided.
     */

    const char *res = "unknown";

    switch( status ) {
    case THERMAL_STATUS_LOW:
      res = thermalmanager_thermal_status_low;
      break;

    case THERMAL_STATUS_NORMAL:
      res = thermalmanager_thermal_status_normal;
      break;

    case THERMAL_STATUS_WARNING:
      res = thermalmanager_thermal_status_warning;
      break;

    case THERMAL_STATUS_ALERT:
      res = thermalmanager_thermal_status_alert;
      break;

    case THERMAL_STATUS_FATAL:
      res = thermalmanager_thermal_status_fatal;
      break;

    default: break;
    }

    return res;
}

/** Convert thermal status to human readable string
 *
 * @param  status thermal status
 *
 * @return name of the status
 */
const char *
thermal_status_repr(THERMAL_STATUS status)
{
    const char *repr = "UNKNOWN";

    switch (status) {
    case THERMAL_STATUS_LOW:     repr = "LOW";     break;
    case THERMAL_STATUS_NORMAL:  repr = "NORMAL";  break;
    case THERMAL_STATUS_WARNING: repr = "WARNING"; break;
    case THERMAL_STATUS_ALERT:   repr = "ALERT";   break;
    case THERMAL_STATUS_FATAL:   repr = "FATAL";   break;
    case THERMAL_STATUS_INVALID: repr = "INVALID"; break;
    default: break;
    }

    return repr;
}

/* ========================================================================= *
 * THERMAL_MANAGER
 * ========================================================================= */

/** Initiate thermal object state update
 *
 * Pass request to thermal sensor via thermal object abstraction layer.
 *
 * The temperature query is assumed to be asynchronous.
 *
 * Control returns to thermal manager when thermal object layer calls
 * thermal_manager_handle_object_update() function.
 *
 * @param thermal_object  registered thermal object
 */
void
thermal_manager_request_object_update(thermal_object_t *thermal_object)
{
    /* Ignore invalid / unregistered objects */
    if( !thermal_manager_object_is_registered(thermal_object) )
        goto EXIT;

    /* Initiate fetching and evaluating of the current value */
    thermal_object_request_update(thermal_object);

    /* ... lower level activity ...
     * -> thermal_object_handle_update()
     *    -> thermal_manager_handle_object_update()
     *       -> thermal_manager_schedule_object_poll()
     */

EXIT:
    return;
}

/** Scedule iphb wakeup for updating thermal object
 *
 * Normally the required poll delay is decided at thermal sensor
 * and depends on the current thermal state for the sensor.
 *
 * However, when status change is detected, several measurements
 * are needed before the new status is accepted. For this purpose
 * shorter polling delays are used while the status is in
 * transitional state.
 *
 * Control returns to thermal manager when iphb wakeup message
 * handler calls thermal_manager_request_object_update() function.
 *
 * @param thermal_object  registered thermal object
 */
void
thermal_manager_schedule_object_poll(thermal_object_t *thermal_object)
{
    /* Ignore invalid / unregistered objects
     */
    if( !thermal_manager_object_is_registered(thermal_object) )
        goto EXIT;

    /* Schedule the next measurement point
     */
    DSM_MSGTYPE_WAIT msg = DSME_MSG_INIT(DSM_MSGTYPE_WAIT);

    msg.req.pid = 0;
    msg.data    = thermal_object;

    /* Start with fall back defaults */
    int mintime = THERMAL_STATUS_POLL_DELAY_DEFAULT_MINIMUM;
    int maxtime = THERMAL_STATUS_POLL_DELAY_DEFAULT_MAXIMUM;

    if( thermal_object_status_in_transition(thermal_object) ) {
        /* In transition: multiple measurements are neede to
         * verify the status change - wake up more frequently */
        mintime = THERMAL_STATUS_POLL_DELAY_TRANSITION_MINIMUM;
        maxtime = THERMAL_STATUS_POLL_DELAY_TRANSITION_MAXIMUM;

        /* and wake up from suspend to do the measurement */
        msg.req.wakeup  = true;
    }
    else if( !thermal_object_get_poll_delay(thermal_object,
                                            &mintime, &maxtime) ) {
        /* No wait period defined in the configuration - use
         * shorter waits for abnormal states */

        THERMAL_STATUS    status = THERMAL_STATUS_INVALID;
        int               temperature = INVALID_TEMPERATURE;

        thermal_object_get_sensor_status(thermal_object, &status,
                                         &temperature);

        switch( status ) {
        case THERMAL_STATUS_ALERT:
        case THERMAL_STATUS_FATAL:
            mintime =  THERMAL_STATUS_POLL_ALERT_TRANSITION_MINIMUM;
            maxtime =  THERMAL_STATUS_POLL_ALERT_TRANSITION_MAXIMUM;
            break;

        default:
            break;
        }
    }

    if( mintime == maxtime ) {
        dsme_log(LOG_DEBUG, PFIX"%s: check again in %d sec global slot",
                 thermal_object_get_name(thermal_object), mintime);
    }
    else {
        dsme_log(LOG_DEBUG, PFIX"%s: check again in %d to %d seconds",
                 thermal_object_get_name(thermal_object), mintime, maxtime);
    }

    msg.req.mintime = mintime;
    msg.req.maxtime = maxtime;

    /* Wakeup will be sent to "originating module". Since
     * this function can end up being called from events
     * dispatched at other modules, we need to maintain
     * the context manually ... */
    const module_t *from_module = current_module();
    enter_module(this_module);
    broadcast_internally(&msg);
    enter_module(from_module);

    /* ... wait for DSM_MSGTYPE_WAKEUP ...
     * -> thermal_manager_request_object_update()
     */

EXIT:
    return;
}

/** Register thermal object to thermal manager
 *
 * Add sensor object to the list of registered objects.
 *
 * Start update cycle:
 *   1. query sensor temperature and status
 *      ... wait for sensor status
 *
 *   2. update device thermal state
 *   3. broadcast device thermal state
 *
 *   4. schedule sensor specific iphb wakeup
 *      ... wait for iphb wakeup
 *
 *   5. go back to 1
 *
 * @param thermal_object  unregistered thermal object
 */
void
thermal_manager_register_object(thermal_object_t *thermal_object)
{
    if( !thermal_object )
        goto EXIT;

    if( thermal_manager_object_is_registered(thermal_object) )
        goto EXIT;

    dsme_log(LOG_DEBUG, PFIX"%s: registered",
             thermal_object_get_name(thermal_object));

    // add the thermal object to the list of know thermal objects
    thermal_objects = g_slist_append(thermal_objects, thermal_object);

    thermal_manager_request_object_update(thermal_object);

EXIT:
    return;
}

/** Unregister thermal object from thermal manager
 *
 * Remove sensor object from the list of registered objects.
 *
 * It is expected that this function will be called only
 * when dsme is on exit path and sensor backend plugins are
 * being unloaded.
 *
 * Pending temperature requests are not canceled as it
 * is assumed to be done by the thermal sensor plugin.
 * If lower levels however do use thermal manager API
 * to report changes later on, the calls are ignored
 * because the objects are no longer registered.
 *
 * Similarly iphb wakeups relating to unregistered objects
 * are ignored. And once thermal manager plugin is unloaded
 * they will not be even dispatched anymore.
 *
 * @param thermal_object  registered thermal object
 */
void
thermal_manager_unregister_object(thermal_object_t *thermal_object)
{
    if( !thermal_object )
        goto EXIT;

    if( !thermal_manager_object_is_registered(thermal_object) )
        goto EXIT;

    // remove the thermal object from the list of know thermal objects
    thermal_objects = g_slist_remove(thermal_objects, thermal_object);

    dsme_log(LOG_DEBUG, PFIX"%s: unregistered",
             thermal_object_get_name(thermal_object));

EXIT:
    return;
}

/** Check if thermal object is registered to thermal manager
 *
 * @param thermal_object  thermal object
 *
 * @return true if object is registerd, false otherwise
 */
bool
thermal_manager_object_is_registered(thermal_object_t *thermal_object)
{
    bool is_registered = false;

    if( !thermal_object )
        goto EXIT;

    if( g_slist_find(thermal_objects, thermal_object) )
        is_registered = true;

EXIT:
    return is_registered;
}

/** Get sensor status and temperature
 *
 * The status reported is the worst deviation from normal status found in
 * the thermal objects that match the sensor_name given.
 *
 * For example, if there are sensors "core0", "core1", "core2" and "core3"
 *
 * Requesting "core0" status returns info from the "core0" as is.
 *
 * But requesting "core" status depends on overall status of all four
 * matching sensors, which is highest state and temperature found, unless
 * no sensors are in grave overheat state and at least one sensor is in
 * low temperature states - in which case lowest state/temperature is
 * returned.
 *
 * Notes:
 *  - The status and temperature values are not modified if no matching
 *    sensors are found
 *  - Values cached at thermal object level are used - the temperature
 *    is always the latest seen, but during lower level status transition
 *    the previous (stable) status is used
 *
 * @param sensor_name  sensor name / sensor group name prefix
 * @param status       where to store sensor thermal status
 * @param temperature  where to store sensor temperature value
 *
 * @param return true if matching sensors were found and output values set;
 *               false otherwise
 */
bool
thermal_manager_get_sensor_status(const char *sensor_name,
                                  THERMAL_STATUS *status, int *temperature)
{
    static volatile int recursing = 0;

    bool ack = false;

    /* In theory backend config parser should eliminate
     * cyclic sensor dependencies - but if it still happens,
     * it must not end up in infinite recursion  ... */
    if( ++recursing != 1 )
        goto EXIT;

    /* Initialize to invalid [lo,hi] range */
    THERMAL_STATUS status_hi = THERMAL_STATUS_LOW;
    THERMAL_STATUS status_lo = THERMAL_STATUS_FATAL;
    int            temp_lo   = IGNORE_TEMP_ABOVE;
    int            temp_hi   = IGNORE_TEMP_BELOW;

    for( GSList *item = thermal_objects; item; item = item->next ) {
        thermal_object_t *object = item->data;

        if( !thermal_object_has_name_like(object, sensor_name) )
            continue;

        THERMAL_STATUS s = THERMAL_STATUS_INVALID;
        int            t = INVALID_TEMPERATURE;

        if( !thermal_object_get_sensor_status(object, &s, &t) )
            continue;

        if( status_hi < s )   status_hi = s;
        if( status_lo > s )   status_lo = s;

        if( temp_hi < t ) temp_hi = t;
        if( temp_lo > t ) temp_lo = t;
    }

    /* Skip if there were no matching sensors */
    if( status_lo > status_hi || temp_lo > temp_hi )
        goto EXIT;

    /* Precedence: FATAL > ALERT > LOW > WARNING > NORMAL
     *
     * There is implicit exceptation that group of matching
     * sensors share similar enough temperature config that
     * this simplistic temperature selection makes sense.
     */

    if( status_lo < THERMAL_STATUS_NORMAL &&
        status_hi < THERMAL_STATUS_ALERT ) {
        *status = status_lo;
        *temperature = temp_lo;
    }
    else {
        *status = status_hi;
        *temperature = temp_hi;
    }

    ack = true;

EXIT:

    --recursing;

    return ack;
}

/** Handle updated thermal object status
 *
 * Called by thermal object layer when
 * a) fresh temperature & status data is available
 * b) temperature query via thermal sensor failed
 *
 * The overall device thermal state is re-evaluated and
 * changes broadcast both internally and externally.
 *
 * The next temperature poll time is scheduled.
 *
 * @param thermal_object  registered thermal object
 */
void
thermal_manager_handle_object_update(thermal_object_t *changed_object)
{
    if( !thermal_manager_object_is_registered(changed_object) )
        goto EXIT;

    /* Scan all thermal objects for lowest/highest status */
    THERMAL_STATUS highest_status = THERMAL_STATUS_NORMAL;
    THERMAL_STATUS lowest_status  = THERMAL_STATUS_NORMAL;
    THERMAL_STATUS overall_status = THERMAL_STATUS_NORMAL;

    for( GSList *item = thermal_objects; item; item = item->next ) {
        thermal_object_t *object = item->data;
        THERMAL_STATUS    status = thermal_object_get_status(object);

        /* Ignore sensors in invalid state */
        if( status == THERMAL_STATUS_INVALID )
            continue;

        if( highest_status < status )
            highest_status = status;

        if( lowest_status > status )
            lowest_status = status;
    }

    /* Decide overall status:
     *  If we have any ALERT of FATAL then that decides overall status
     *  During LOW, NORMAL or WARNING, any LOW wins
     *  Else status is the highest
     */
    if( lowest_status  < THERMAL_STATUS_NORMAL &&
        highest_status < THERMAL_STATUS_ALERT )
        overall_status = lowest_status;
    else
        overall_status = highest_status;

    /* Send notifications */
    thermal_manager_broadcast_status(overall_status, changed_object);

    /* Schedule the next inspection point */
    thermal_manager_schedule_object_poll(changed_object);

EXIT:
    return;
}

/** Update current overall device thermal status
 *
 * Update bookkeeping and broadcast status both within dsme
 * and via dbus.
 *
 * @param status          device overall thermal status
 * @param changed_object  thermal object that caused status change
 */
static void
thermal_manager_broadcast_status(THERMAL_STATUS status,
                                 thermal_object_t *changed_object)
{
    /* Skip broadcast if no change */
    if( current_status == status )
        goto EXIT;

    current_status = status;

    /* First send an indication to D-Bus */
    thermal_manager_broadcast_status_dbus(status);

    /* Then broadcast an indication internally */

    /* Note: The level gets attributed to the sensor that caused the
     *       change. Which is not wrong when elevating level, but can
     *       be completely bogus when returning to more normal state
     *       after having several sensors in elevated status.
     *
     *       Since consumers of this if should not care what is the
     *       sensor that is reported, leaving this the way it has
     *       always been...
     */

    int         temperature = thermal_object_get_temperature(changed_object);
    const char *sensor_name = thermal_object_get_name(changed_object);

    thermal_manager_broadcast_status_dsme(status, temperature, sensor_name);

EXIT:
    return;
}

/** Broadcast overall device thermal status within dsme
 *
 * Map sensor thermal status to device status levels used by
 * for example shutdown policy engine.
 *
 * Broadcast the changes via DSM_MSGTYPE_SET_THERMAL_STATUS event.
 *
 * @param status          device overall thermal status
 * @param temperature     temperature to report
 * @param sensor_name     sensor to report as cause of the change
 */
static void
thermal_manager_broadcast_status_dsme(THERMAL_STATUS status,
                                      int temperature,
                                      const char *sensor_name)
{
    static dsme_thermal_status_t prev = DSM_THERMAL_STATUS_NORMAL;

    /* Map sensor status to device status */
    dsme_thermal_status_t curr = DSM_THERMAL_STATUS_NORMAL;

    switch( status ) {
    case THERMAL_STATUS_LOW:
        curr = DSM_THERMAL_STATUS_LOWTEMP;
        break;

    default:
    case THERMAL_STATUS_INVALID:
    case THERMAL_STATUS_NORMAL:
    case THERMAL_STATUS_WARNING:
    case THERMAL_STATUS_ALERT:
        break;

    case THERMAL_STATUS_FATAL:
        curr = DSM_THERMAL_STATUS_OVERHEATED;
        break;
    }

    /* Skip broadcast if no change */
    if( prev == curr )
        goto EXIT;

    prev = curr;

    /* Log state change */
    switch( curr ) {
    case DSM_THERMAL_STATUS_LOWTEMP:
        dsme_log(LOG_WARNING, PFIX"policy: low temperature (%s %dC)",
                 sensor_name, temperature);
        break;

    default:
    case DSM_THERMAL_STATUS_NORMAL:
        dsme_log(LOG_NOTICE, PFIX"policy: acceptable temperature (%s %dC)",
                 sensor_name, temperature);
        break;

    case DSM_THERMAL_STATUS_OVERHEATED:
        dsme_log(LOG_CRIT, PFIX"policy: overheated (%s %dC)", sensor_name,
                 temperature);
        break;
    }

    /* Broadcast state change */
    DSM_MSGTYPE_SET_THERMAL_STATUS msg =
        DSME_MSG_INIT(DSM_MSGTYPE_SET_THERMAL_STATUS);

    msg.status      = curr;
    msg.temperature = temperature;
    strncat(msg.sensor_name, sensor_name, sizeof msg.sensor_name - 1);

    broadcast_internally(&msg);

EXIT:
    return;
}

/** Broadcast overall device thermal status via D-Bus
 *
 * Broadcast the changes as D-Bus signal
 *   com.nokia.thermalmanager.thermal_state_change_ind(state_name)
 *
 * @param status          device overall thermal status
 */
static void
thermal_manager_broadcast_status_dbus(THERMAL_STATUS status)
{
    static THERMAL_STATUS prev = THERMAL_STATUS_NORMAL;

    if( prev == status )
        goto EXIT;

    prev = status;

    const char *arg = thermal_status_name(status);

    dsme_log(LOG_NOTICE, PFIX"send dbus signal %s.%s(%s)",
             thermalmanager_interface,
             thermalmanager_state_change_ind, arg);

    DsmeDbusMessage *sig =
        dsme_dbus_signal_new(thermalmanager_path,
                             thermalmanager_interface,
                             thermalmanager_state_change_ind);

    dsme_dbus_message_append_string(sig, arg);
    dsme_dbus_signal_emit(sig);

EXIT:
    return;
}

/** Request sensor status update
 *
 * All thermal objects have name matching the one given are
 * updated.
 *
 * For example, if there are sensors "core0", "core1", "core2" and "core3"
 *
 * Requesting "core0" update does "core0" only.
 *
 * But requesting "core" update does all four.
 *
 * @param sensor_name  sensor name / sensor group name prefix
 */
bool
thermal_manager_request_sensor_update(const char *sensor_name)
{
    bool ack = false;
    for( GSList *item = thermal_objects; item; item = item->next ) {
        thermal_object_t *object = item->data;
        if( !thermal_object_has_name_like(object, sensor_name) )
            continue;
        thermal_object_request_update(object);
        ack = true;
        break;
    }
    return ack;
}

/** Check for pending sensor status request
 *
 * Check if any thermal objects with name matching the given on
 * are still waiting for status update.
 *
 * For example, if there are sensors "core0", "core1", "core2" and "core3"
 *
 * Requesting "core0" checks "core0" only.
 *
 * But requesting "core" checks all four.
 *
 * @param sensor_name  sensor name / sensor group name prefix
 */
static bool
thermal_manager_have_pending_sensor_update(const char *sensor_name)
{
    bool pending = false;

    for( GSList *item = thermal_objects; item; item = item->next ) {
        thermal_object_t *object = item->data;

        /* Must be waiting for status */
        if( !thermal_object_update_is_pending(object) )
            continue;

        /* And matching the name */
        if( !thermal_object_has_name_like(object, sensor_name) )
            continue;

        /* But self-dependencies must not be allowed */
        if( thermal_object_has_name(object, sensor_name) )
            continue;

        pending = true;
        break;
    }

    return pending;
}

/** Update sensors that depend on the given thermal object
 *
 * If there are meta sensors that depend on temperature
 * of the sensor that was updated, this function will
 * re-evaluate the depending sensors.
 *
 * @param changed_object  thermal object that just got updated
 */
void
thermal_manager_handle_sensor_update(const thermal_object_t *changed_object)
{
    const char *sensor_name = thermal_object_get_name(changed_object);

    for( GSList *item = thermal_objects; item; item = item->next ) {
        thermal_object_t *object = item->data;

        /* Must be waiting for status */
        if( !thermal_object_update_is_pending(object) )
            continue;

        /* and depending on the changed sensor */
        const char *depends_on = thermal_object_get_depends_on(object);
        if( !depends_on )
            continue;

        if( !thermal_object_has_name_like(changed_object, depends_on) )
            continue;

        /* but self-dependency must not be allowed */
        if( thermal_object_has_name(object, sensor_name) )
            continue;

        /* in case it is a group dependency, check all matching sensors */
        if( thermal_manager_have_pending_sensor_update(depends_on) )
            continue;

        /* Initiate sensor re-evaluation at backend. When finished,
         * a call to thermal_object_handle_update() is made.
         */
        if( !thermal_object_read_sensor(object) )
            thermal_object_cancel_update(object);

        // -> thermal_object_handle_update(object);
    }
}

/* ========================================================================= *
 * DBUS_HANDLERS
 * ========================================================================= */

/* Handle com.nokia.thermalmanager.get_thermal_state D-Bus method call
 *
 * @param req   D-Bus method call message
 * @param rsp   Where to store D-Bus method return message
 */
static void
thermal_manager_get_thermal_state_cb(const DsmeDbusMessage *req,
                                     DsmeDbusMessage **rsp)
{
    *rsp = dsme_dbus_reply_new(req);
    dsme_dbus_message_append_string(*rsp, thermal_status_name(current_status));
}

/** Helper for handling temperature query D-Bus method calls
 *
 * @param req          D-Bus method call message
 * @param sensor_name  Sensor name / sensor group name prefix
 * @param rsp          Where to store D-Bus method return message
 */
static void
thermal_manager_handle_temperature_query(const DsmeDbusMessage *req,
                                         const char *sensor_name,
                                         DsmeDbusMessage **rsp)
{
    THERMAL_STATUS    status      = THERMAL_STATUS_INVALID;
    int               temperature = INVALID_TEMPERATURE;

    thermal_manager_get_sensor_status(sensor_name, &status, &temperature);
    *rsp = dsme_dbus_reply_new(req);
    dsme_dbus_message_append_int(*rsp, temperature);
}

/* Handle com.nokia.thermalmanager.estimate_surface_temperature method call
 *
 * Provides backwards compatibility with legacy D-Bus method
 * that was originally implemented in Harmattan specific
 * sensor backend plugin.
 *
 * @param req   D-Bus method call message
 * @param rsp   Where to store D-Bus method return message
 */
static void
thermal_manager_get_surface_temperature_cb(const DsmeDbusMessage *req,
                                           DsmeDbusMessage **rsp)
{
    thermal_manager_handle_temperature_query(req, "surface", rsp);
}

/* Handle com.nokia.thermalmanager.core_temperature D-Bus method call
 *
 * Provides backwards compatibility with legacy D-Bus method
 * that was originally implemented in Harmattan specific
 * sensor backend plugin.
 *
 * @param req   D-Bus method call message
 * @param rsp   Where to store D-Bus method return message
 */
static void
thermal_manager_get_core_temperature_cb(const DsmeDbusMessage *req,
                                        DsmeDbusMessage **rsp)
{
    thermal_manager_handle_temperature_query(req, "core", rsp);
}

/* Handle com.nokia.thermalmanager.battery_temperature D-Bus method call
 *
 * Provides backwards compatibility with legacy D-Bus method
 * that was originally implemented in Harmattan specific
 * sensor backend plugin.
 *
 * @param req   D-Bus method call message
 * @param rsp   Where to store D-Bus method return message
 */
static void
thermal_manager_get_battery_temperature_cb(const DsmeDbusMessage *req,
                                           DsmeDbusMessage **rsp)
{
    thermal_manager_handle_temperature_query(req, "battery", rsp);
}

/* Handle com.nokia.thermalmanager.sensor_temperature D-Bus method call
 *
 * Provides backwards compatibility with legacy D-Bus method
 * that was originally implemented in Harmattan specific
 * sensor backend plugin.
 *
 * @param req   D-Bus method call message
 * @param rsp   Where to store D-Bus method return message
 */
static void
thermal_manager_get_sensor_temperature_cb(const DsmeDbusMessage *req,
                                          DsmeDbusMessage **rsp)
{
    const char *sensor = dsme_dbus_message_get_string(req);
    thermal_manager_handle_temperature_query(req, sensor, rsp);
}

/** Array of D-Bus method calls supported by this plugin */
static const dsme_dbus_binding_t dbus_methods_lut[] =
{
    { thermal_manager_get_thermal_state_cb,       thermalmanager_get_thermal_state },
    { thermal_manager_get_surface_temperature_cb, thermalmanager_estimate_surface_temperature },
    { thermal_manager_get_core_temperature_cb,    thermalmanager_core_temperature  },
    { thermal_manager_get_battery_temperature_cb, thermalmanager_battery_temperature },
    { thermal_manager_get_sensor_temperature_cb,  thermalmanager_sensor_temperature },

    { 0, 0 }
};

/* ========================================================================= *
 * MESSAGE_HANDLERS
 * ========================================================================= */

/** Handler for iphb wakeup events
 */
DSME_HANDLER(DSM_MSGTYPE_WAKEUP, client, msg)
{
    thermal_object_t *thermal_object = msg->data;

    if( !thermal_manager_object_is_registered(thermal_object) )
        goto EXIT;

    thermal_manager_request_object_update(thermal_object);

EXIT:
    return;
}

/** Handler for connected to D-Bus system bus event
 */
DSME_HANDLER(DSM_MSGTYPE_DBUS_CONNECT, client, msg)
{
    dsme_log(LOG_DEBUG, PFIX"DBUS_CONNECT");

    /* Add dbus method call handlers */
    dsme_dbus_bind_methods(&dbus_methods_bound, dbus_methods_lut,
                           thermalmanager_service, thermalmanager_interface);
}

/** Handler for disconnected to D-Bus system bus event
 */
DSME_HANDLER(DSM_MSGTYPE_DBUS_DISCONNECT, client, msg)
{
    dsme_log(LOG_DEBUG, PFIX"DBUS_DISCONNECT");

    /* Remove dbus method call handlers */
    dsme_dbus_unbind_methods(&dbus_methods_bound, dbus_methods_lut,
                             thermalmanager_service, thermalmanager_interface);
}

/** Array of DSME event handlers implemented by this plugin */
module_fn_info_t message_handlers[] =
{
    DSME_HANDLER_BINDING(DSM_MSGTYPE_WAKEUP),
    DSME_HANDLER_BINDING(DSM_MSGTYPE_DBUS_CONNECT),
    DSME_HANDLER_BINDING(DSM_MSGTYPE_DBUS_DISCONNECT),
    { 0 }
};

/* ========================================================================= *
 * MODULE_GLUE
 * ========================================================================= */

/** Init hook that DSME plugins need to implement
 *
 * @param handle DSME plugin handle
 */
void
module_init(module_t *handle)
{
    dsme_log(LOG_DEBUG, PFIX"loaded");

    this_module = handle;
}

/** Exit hook that DSME plugins need to implement
 */
void
module_fini(void)
{
    /* Clear remaining thermal objects from the registered list */
    if( thermal_objects ) {
        dsme_log(LOG_ERR, PFIX"registered thermal objects remain "
                 "at unload time");

        do
            thermal_manager_unregister_object(thermal_objects->data);
        while( thermal_objects );
    }

    /* Remove dbus method call handlers */
    dsme_dbus_unbind_methods(&dbus_methods_bound, dbus_methods_lut,
                             thermalmanager_service, thermalmanager_interface);

    dsme_log(LOG_DEBUG, PFIX"unloaded");
}
