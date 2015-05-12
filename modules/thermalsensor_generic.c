/**
   @file thermalsensor_generic.c

   This file implements a thermal object for tracking HW temperatures.
   Battery and SOC core temperatures
   <p>
   Copyright (C) 2015 Jolla ltd

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

/* ========================================================================= *
 * DSME
 * ========================================================================= */

#include "thermalmanager.h"

#include "../include/dsme/modules.h"
#include "../include/dsme/logging.h"

#include "dbusproxy.h"

/* ========================================================================= *
 * GENERIC
 * ========================================================================= */

#include <sys/types.h>
#include <sys/stat.h>

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <syslog.h>
#include <glob.h>

#include <glib.h>

/* ========================================================================= *
 * DIAGNOSTIC_LOGGING
 * ========================================================================= */

#define PFIX "thermal sensor generic: "

/* ========================================================================= *
 * UTILITY_FUNCTIONS
 * ========================================================================= */

static char *tsg_util_slice_token  (char *pos, char **ppos, int sep);
static void  tsg_util_set_string   (char **ptr, const char *val);
static char *tsg_util_read_file    (const char *path);
static bool  tsg_util_write_file   (const char *path, const char *text);
static bool  tsg_util_parse_int    (const char *text, int *value);
static bool  tsg_util_read_int     (const char *path, int *value, int divisor);

static bool  tsg_util_read_temp_C  (const char *path, int *temp);
static bool  tsg_util_read_temp_dC (const char *path, int *temp);
static bool  tsg_util_read_temp_mC (const char *path, int *temp);
static bool  tsg_util_read_other   (const char *sensor, int *temp);

/* ========================================================================= *
 * THERMAL_SENSOR_GENERIC
 * ========================================================================= */

/** Callback function type for reading temperature from a file */
typedef bool (*sg_temp_fn)(const char *path, int *temp);

/** Configuration data for thermal status level */
typedef struct
{
    /** Lower temperature bound for thermal status */
    int sl_mintemp;

    /** Minimum poll delay while in this thermal state */
    int sl_minwait;

    /** Maximum poll delay while in this thermal state */
    int sl_maxwait;
} sensor_level_t;

/** State data for one thermal sensor */
typedef struct
{
    /** Sensor name */
    char              *sg_name;

    /** Cached temperature [C] */
    int                sg_temp;

    /** Cached thermal status */
    THERMAL_STATUS     sg_status;

    /** Temperature read function */
    sg_temp_fn         sg_temp_cb;

    /** Temperature file path / dependency sensor name */
    char              *sg_temp_path;

    /** Temperature correction offset */
    int                sg_temp_offs;

    /** Flag for: temperature is read from another sensor */
    bool               sg_is_meta;

    /** Path to sensor enable/disable control file */
    char              *sg_mode_path;

    /** Value to write when enabling sensor */
    char              *sg_mode_enable;

    /** Value to write when disabling sensor */
    char              *sg_mode_disable;

    /** Idle callback identifier for thermal object notifications */
    guint              sg_notify_id;

    /** Thermal level configuration array */
    sensor_level_t     sg_level[THERMAL_STATUS_COUNT];

} thermal_sensor_generic_t;

static thermal_sensor_generic_t  *thermal_sensor_generic_create             (const char *name);
static void                       thermal_sensor_generic_delete             (thermal_sensor_generic_t *self);

static bool                       thermal_sensor_generic_is_valid           (thermal_sensor_generic_t *self);

static const char                *thermal_sensor_generic_get_name           (const thermal_sensor_generic_t *self);
static bool                       thermal_sensor_generic_get_status         (const thermal_sensor_generic_t *self, int *temp, THERMAL_STATUS *status);
static const char                *thermal_sensor_generic_get_depends_on     (const thermal_sensor_generic_t *self);
static bool                       thermal_sensor_generic_get_poll_delay     (const thermal_sensor_generic_t *self, int *minwait, int *maxwait);

static bool                       thermal_sensor_generic_enable_sensor      (const thermal_sensor_generic_t *self, bool enable);
static bool                       thermal_sensor_generic_sensor_is_enabled  (const thermal_sensor_generic_t *self);
static bool                       thermal_sensor_generic_read_sensor        (thermal_sensor_generic_t *self);

static void                       thermal_sensor_generic_set_temp_path      (thermal_sensor_generic_t *self, const char *path);
static void                       thermal_sensor_generic_set_temp_func      (thermal_sensor_generic_t *self, sg_temp_fn cb);
static void                       thermal_sensor_generic_set_mode_control   (thermal_sensor_generic_t *self, const char *path, const char *enable, const char *disable);
static void                       thermal_sensor_generic_set_limit          (thermal_sensor_generic_t *self, THERMAL_STATUS status, int mintemp, int minwait, int maxwait);

static void                       thermal_sensor_generic_set_depends_on     (thermal_sensor_generic_t *self, const char *sensor_name);
static void                       thermal_sensor_generic_set_temp_offs      (thermal_sensor_generic_t *self, int offs);

/* ========================================================================= *
 * HOOKS_FOR_THERMAL_OBJECT
 * ========================================================================= */

static thermal_sensor_generic_t  *thermal_sensor_generic_from_object        (const thermal_object_t *object);
static void                       thermal_sensor_generic_delete_cb          (thermal_object_t *object);
static const char                *thermal_sensor_generic_get_name_cb        (const thermal_object_t *object);
static const char                *thermal_sensor_generic_get_depends_on_cb  (const thermal_object_t *object);
static bool                       thermal_sensor_generic_get_status_cb      (const thermal_object_t *object, THERMAL_STATUS *status, int *temp);
static bool                       thermal_sensor_generic_get_poll_delay_cb  (const thermal_object_t *object, int *minwait, int *maxwait);
static bool                       thermal_sensor_generic_read_sensor_cb     (thermal_object_t *object);

/* ========================================================================= *
 * CONFIGURATION_FILE
 * ========================================================================= */

/** Keyword for declaring the name of the sensor to configure */
#define CONFIG_KW_NAME    "Name"

/** Keyword for declaring path to temperature file */
#define CONFIG_KW_TEMP    "Temp"

/** Keyword for declaring dependency to another sensor */
#define CONFIG_KW_META    "Meta"

/** Keyword for declaring path to enable/disable file */
#define CONFIG_KW_MODE    "Mode"

/** Keyword for declaring limits for low thermal status */
#define CONFIG_KW_LOW     "Low"

/** Keyword for declaring limits for normal thermal status */
#define CONFIG_KW_NORMAL  "Normal"

/** Keyword for declaring limits for warning thermal status */
#define CONFIG_KW_WARNING "Warning"

/** Keyword for declaring limits for alert thermal status */
#define CONFIG_KW_ALERT   "Alert"

/** Keyword for declaring limits for fatal thermal status */
#define CONFIG_KW_FATAL   "Fatal"

/** Keyword for declaring limits for invalid thermal status */
#define CONFIG_KW_INVALID "Invalid"

static thermal_object_t          *tsg_objects_get_object          (GSList *list, const char *name);
static thermal_object_t          *tsg_objects_add_object          (GSList **list, const char *name);
static thermal_sensor_generic_t  *tsg_objects_add_sensor          (GSList **list, const char *name);
static THERMAL_STATUS             tsg_objects_parse_level         (const char *key);
static void                       tsg_objects_register_all        (GSList **list);
static void                       tsg_objects_read_config         (GSList **list, const char *config);
static void                       tsg_objects_quit                (GSList **list);
static void                       tsg_objects_init                (GSList **list);

/* ========================================================================= *
 * UTILITY_FUNCTIONS
 * ========================================================================= */

/** Extract token from string buffer
 *
 * Leading white space at parse position is always skipped.
 *
 * Once parse position is at end of string, empty tokens
 * are returned.
 *
 * @param pos   parse start position
 * @param ppos  where to store position after parsing
 * @param sep   token separator character, or -1 for any whitespace
 *
 * @return extracted token
 */
static char *
tsg_util_slice_token(char *pos, char **ppos, int sep)
{
    unsigned char *beg = (unsigned char *)pos;

    while( *beg > 0 && *beg <= 32 )
        ++beg;

    unsigned char *end = beg;

    if( sep < 0 ) {
        while( *end > 32 )
            ++end;
    }
    else {
        while( *end > 0 && *end != sep )
            ++end;
    }

    if( *end )
        *end++ = 0;

    if( ppos )
        *ppos = (char *)end;

    return (char *)beg;
}

/** Parse key name from config line
 *
 * Slices key name terminated by ':' from parse position.
 *
 * Parse position is updated so that it points to data
 * remaining after sliced value.
 *
 * @param ppos parse position
 *
 * @return key name
 */
static char *
tsg_util_slice_key(char **ppos)
{
  return tsg_util_slice_token(*ppos, ppos, ':');
}

/** Parse string value from config line
 *
 * Slices string terminated by any whitespace character
 * from parse position.
 *
 * Parse position is updated so that it points to data
 * remaining after sliced value.
 *
 * @param ppos parse position
 *
 * @return string value
 */
static char *
tsg_util_slice_str(char **ppos)
{
  return tsg_util_slice_token(*ppos, ppos, -1);
}

/** Parse integer value from config line
 *
 * Slices string terminated by any whitespace character
 * from parse position and convert it to integer.
 *
 * Parse position is updated so that it points to data
 * remaining after sliced value.
 *
 * @param ppos parse position
 *
 * @return integer value
 */
static int
tsg_util_slice_int(char **ppos)
{
  return strtol(tsg_util_slice_str(ppos), 0, 0);
}

/** Helper for setting dynamic string variable
 *
 * @param ptr  pointer to string variable
 * @param val  string, or NULL
 */
static void
tsg_util_set_string(char **ptr, const char *val)
{
    char *use = (val && *val) ? strdup(val) : 0;
    free(*ptr), *ptr = use;
}

/** Get content of a text file as a string
 *
 * @param path  file path
 *
 * @return content of file, or NULL in case of errors
 */
static char *
tsg_util_read_file(const char *path)
{
    char *text = 0;
    int   file = -1;
    char *buff = 0;
    int   size = 512;
    int   used = 0;

    file = TEMP_FAILURE_RETRY(open(path,O_RDONLY));
    if( file == -1 )
        goto EXIT;

    buff = malloc(size + 1);
    if( !buff )
        goto EXIT;

    for( ;; ) {
        int want = size - used;
        int have = TEMP_FAILURE_RETRY(read(file, buff+used, want));

        if( have < 0 )
            goto EXIT;

        used += have;

        if( have < want )
            break;

        size = size * 2;

        char *temp = realloc(buff, size + 1);
        if( !temp )
            break;

        buff = temp;
    }

    buff[used] = 0;

    text = buff, buff = 0;

EXIT:
    free(buff);

    if( file != -1 )
        TEMP_FAILURE_RETRY(close(file));

    return text;
}

/** Write string to an already existing file
 *
 * @param path  file path
 * @param text  content to write
 *
 * @return true if all data could be written, false otherwise
 */
static bool
tsg_util_write_file(const char *path, const char *text)
{
    bool ack  = false;
    int  file = -1;
    int  todo = strlen(text);

    file = TEMP_FAILURE_RETRY(open(path,O_WRONLY));
    if( file == -1 )
        goto EXIT;

    while( todo > 0 ) {
        int done = TEMP_FAILURE_RETRY(write(file, text, todo));
        if( done < 0 )
            goto EXIT;

        text += done;
        todo -= done;
    }

    ack = true;

EXIT:

    if( file != -1 )
        TEMP_FAILURE_RETRY(close(file));

    return ack;
}

/** Convert string to integer value
 *
 * The string must contain number and nothing else
 * but a number.
 *
 * @param text    string to convert
 * @param value   where to store integer value
 *
 * @return true if value was obtained, false otherwise
 */
static bool
tsg_util_parse_int(const char *text, int *value)
{
    bool  ack = false;
    char *end = 0;
    int   val = strtol(text, &end, 0);

    if( end > text )
        *value = val, ack = true;

    return ack;
}

/** Read integer value from a text file
 *
 * @param path     file path
 * @param value    where to store number value
 * @param divisor  downscale factor to apply
 *
 * @return true if value was obtained, false otherwise
 */

static bool
tsg_util_read_int(const char *path, int *value, int divisor)
{
    bool  ack = false;
    char *txt = 0;
    int   val = 0;

    if( !(txt = tsg_util_read_file(path)) )
        goto EXIT;

    if( !tsg_util_parse_int(txt, &val) )
        goto EXIT;

    if( divisor > 1 ) {
        if( val < 0 ) val -= divisor/2;
        else          val += divisor/2;
        val /= divisor;
    }

    *value = val, ack = true;

EXIT:
    free(txt);

    return ack;
}

/** Read a text file containing temperature in [C] units
 *
 * @param path     file path
 * @param temp     where to store the temperature [C]
 *
 * @return true if temperature was obtained, false otherwise
 */
static bool
tsg_util_read_temp_C(const char *path, int *temp)
{
    return tsg_util_read_int(path, temp, 1);
}

/** Read a text file containing temperature in [dC] units
 *
 * @param path     file path
 * @param temp     where to store the temperature [C]
 *
 * @return true if temperature was obtained, false otherwise
 */
static bool
tsg_util_read_temp_dC(const char *path, int *temp)
{
    return tsg_util_read_int(path, temp, 10);
}

/** Read a text file containing temperature in [mC] units
 *
 * @param path     file path
 * @param temp     where to store the temperature [C]
 *
 * @return true if temperature was obtained, false otherwise
 */
static bool
tsg_util_read_temp_mC(const char *path, int *temp)
{
    return tsg_util_read_int(path, temp, 1000);
}

/** Get temperature of named sensor from thermal manager
 *
 * @param path     sensor name / sensor group prefix
 * @param temp     where to store the temperature [C]
 *
 * @return true if temperature was obtained, false otherwise
 */
static bool
tsg_util_read_other(const char *sensor, int *temp)
{
    THERMAL_STATUS status = THERMAL_STATUS_INVALID;
    return thermal_manager_get_sensor_status(sensor, &status, temp);
}

/* ========================================================================= *
 * THERMAL_SENSOR_GENERIC
 * ========================================================================= */

/** Create thermal sensor object
 *
 * @param name  sensor name
 *
 * @return sensor object
 */
static thermal_sensor_generic_t *
thermal_sensor_generic_create(const char *name)
{
    thermal_sensor_generic_t *self = calloc(1, sizeof *self);

    self->sg_name         = strdup(name);
    self->sg_temp         = INVALID_TEMPERATURE;
    self->sg_status       = THERMAL_STATUS_INVALID;

    self->sg_temp_cb      = 0;
    self->sg_temp_path    = 0;
    self->sg_temp_offs    = 0;
    self->sg_is_meta      = false;

    self->sg_mode_path    = 0;
    self->sg_mode_enable  = 0;
    self->sg_mode_disable = 0;

    self->sg_notify_id    = 0;

    for( int i = 0; i < THERMAL_STATUS_COUNT; ++i ) {
        self->sg_level[i].sl_mintemp = INVALID_TEMPERATURE;
        self->sg_level[i].sl_minwait = 0;
        self->sg_level[i].sl_maxwait = 0;
    }

    return self;
}

/** Delete thermal sensor object
 *
 * @param self  sensor object, or NULL
 */
static void
thermal_sensor_generic_delete(thermal_sensor_generic_t *self)
{
    if( !self )
        goto EXIT;

    if( self->sg_notify_id ) {
        g_source_remove(self->sg_notify_id),
            self->sg_notify_id = 0;
    }

    free(self->sg_name);

    free(self->sg_temp_path);

    free(self->sg_mode_path);
    free(self->sg_mode_enable);
    free(self->sg_mode_disable);

    free(self);

EXIT:
    return;
}

/** Check if sensor object is valid
 *
 * Check that all necessary values are set and configured paths
 * exist and are readable/writable as applicable.
 *
 * @param self  sensor object, or NULL
 *
 * @return true if sensor object is valid, false otherwise
 */
static bool
thermal_sensor_generic_is_valid(thermal_sensor_generic_t *self)
{
    bool is_valid = false;

    if( !self )
        goto EXIT;

    if( !self->sg_name ) {
        dsme_log(LOG_ERR, PFIX"sensor object without a name");
        goto EXIT;
    }

    if( !self->sg_temp_cb ) {
        dsme_log(LOG_ERR, PFIX"%s: %s",
                 thermal_sensor_generic_get_name(self),
                 "no temperature callback defined");
        goto EXIT;
    }

    if( !self->sg_temp_path ) {
        dsme_log(LOG_ERR, PFIX"%s: %s",
                 thermal_sensor_generic_get_name(self),
                 "no temperature source defined");
        goto EXIT;
    }

    if( !self->sg_is_meta &&
        access(self->sg_temp_path, R_OK) != 0 ) {
        dsme_log(LOG_ERR, PFIX"%s: %s: %m",
                 thermal_sensor_generic_get_name(self), self->sg_temp_path);
        goto EXIT;
    }

    if( self->sg_mode_path ) {
        if( self->sg_is_meta ) {
            dsme_log(LOG_ERR, PFIX"%s: %s",
                     thermal_sensor_generic_get_name(self),
                     "mode control file specified for meta sensor");
            goto EXIT;
        }

        if( access(self->sg_mode_path, W_OK) != 0 ) {
            dsme_log(LOG_ERR, PFIX"%s: %s: %m",
                     thermal_sensor_generic_get_name(self),
                     self->sg_mode_path);
            goto EXIT;
        }

        if( !self->sg_mode_enable ) {
            dsme_log(LOG_ERR, PFIX"%s: %s",
                     thermal_sensor_generic_get_name(self),
                     "enable value not defined");
            goto EXIT;
        }
        /* Note: Leaving disable string undefined is ok and is
         *       taken to mean: enable sensor on dsme startup,
         *       leave it enabled on dsme exit.
         */
    }

    for( int i = 0; i < THERMAL_STATUS_COUNT; ++i ) {
        if( self->sg_level[i].sl_mintemp == INVALID_TEMPERATURE ) {
            dsme_log(LOG_ERR, PFIX"%s: %s",
                     thermal_sensor_generic_get_name(self),
                     "temperature limits not defined");
            goto EXIT;
        }
        if( self->sg_level[i].sl_minwait < 1 ||
            self->sg_level[i].sl_maxwait < 5 ||
            self->sg_level[i].sl_maxwait < self->sg_level[i].sl_minwait ) {
            dsme_log(LOG_ERR, PFIX"%s: %s",
                     thermal_sensor_generic_get_name(self),
                     "invalid wait period defined");
            goto EXIT;
        }
    }

    for( int i = 1; i < THERMAL_STATUS_COUNT; ++i ) {
        if( self->sg_level[i-1].sl_mintemp > self->sg_level[i].sl_mintemp ) {
            dsme_log(LOG_ERR, PFIX"%s: %s",
                     thermal_sensor_generic_get_name(self),
                     "temperature limits not ascending");
            goto EXIT;
        }
    }

    is_valid = true;

EXIT:

    return is_valid;
}

/** Get sensor name from sensor object
 *
 * @param self  sensor object, or NULL
 *
 * @return sensor name, or "invalid"
 */
static const char *
thermal_sensor_generic_get_name(const thermal_sensor_generic_t *self)
{
    const char *name = "invalid";
    if( self )
        name = self->sg_name;
    return name ?: "unknown";
}

/** Get name of sensor object object depends on
 *
 * @param self  sensor object
 *
 * @return sensor name, or NULL
 */
static const char *
thermal_sensor_generic_get_depends_on(const thermal_sensor_generic_t *self)
{
    const char *depends_on = 0;
    if( self && self->sg_is_meta )
        depends_on = self->sg_temp_path;
    return depends_on;
}

/** Get name of sensor object object depends on
 *
 * @param self    sensor object
 * @param temp    where to store temperature [C]
 * @param status  where to store thermal status
 *
 * @return true if values were obtained, false otherwise
 */
static bool
thermal_sensor_generic_get_status(const thermal_sensor_generic_t *self,
                         int *temp, THERMAL_STATUS *status)
{
    *temp   = self->sg_temp;
    *status = self->sg_status;

    return self->sg_status != THERMAL_STATUS_INVALID;
}

/** Get sensor object poll delay
 *
 * @param self    sensor object
 * @param minwait  where to store minumum wait [s]
 * @param maxwait  where to store maximum wait [s]
 *
 * @return true if values were obtained, false otherwise
 */
static bool
thermal_sensor_generic_get_poll_delay(const thermal_sensor_generic_t *self,
                                      int *minwait, int *maxwait)
{
    bool ack = false;

    if( !self )
        goto EXIT;

    int lo = self->sg_level[self->sg_status].sl_minwait;
    int hi = self->sg_level[self->sg_status].sl_maxwait;

    if( lo <= 0 || hi < lo )
        goto EXIT;

    *minwait = lo;
    *maxwait = hi;
    ack = true;

EXIT:
    return ack;
}

/** Enable/disable sensor associated with sensor object
 *
 * @param self    sensor object
 * @param enable  true to enable, false to disable
 *
 * @return true if setting succeeded, false otherwise
 */
static bool
thermal_sensor_generic_enable_sensor(const thermal_sensor_generic_t *self,
                                     bool enable)
{
    bool ack = true;

    if( !self->sg_mode_path )
        goto EXIT;

    const char *mode = enable ? self->sg_mode_enable : self->sg_mode_disable;

    if( !mode )
        goto EXIT;

    ack = tsg_util_write_file(self->sg_mode_path, mode);

EXIT:
    return ack;
}

static bool
thermal_sensor_generic_sensor_is_enabled(const thermal_sensor_generic_t *self)
{
    bool is_enabled = true;

    char *mode = 0;

    if( !self->sg_mode_path || !self->sg_mode_enable )
        goto EXIT;

    is_enabled = false;

    if( !(mode = tsg_util_read_file(self->sg_mode_path)) ) {
        dsme_log(LOG_WARNING, PFIX"%s: failed to read sensor mode: %m",
                 thermal_sensor_generic_get_name(self));
        goto EXIT;
    }

    mode[strcspn(mode, "\r\n")] = 0;

    is_enabled = !strcmp(mode, self->sg_mode_enable);

EXIT:
    free(mode);

    return is_enabled;
}

/** Read sensor associated with sensor object
 *
 * On success the value is cached and can be obtained via
 * thermal_sensor_generic_get_status() function.
 *
 * @param self    sensor object
 *
 * @return true if reading succeeded, false otherwise
 */
static bool
thermal_sensor_generic_read_sensor(thermal_sensor_generic_t *self)
{
    bool ack = false;

    int            temp   = INVALID_TEMPERATURE;
    THERMAL_STATUS status = THERMAL_STATUS_INVALID;

    if( !self )
        goto EXIT;

    if( !self->sg_temp_cb )
        goto EXIT;

    if( !self->sg_temp_cb(self->sg_temp_path, &temp) ) {

        /* Check if the failure could be because some other
         * process has disabled the sensor */

        if( thermal_sensor_generic_sensor_is_enabled(self) )
            goto EXIT;

        /* Attempt to re-enale and read again */

        dsme_log(LOG_WARNING, PFIX"%s: sensor is disabled; re-enabling",
                 thermal_sensor_generic_get_name(self));

        if( !thermal_sensor_generic_enable_sensor(self, true) ) {
            dsme_log(LOG_WARNING, PFIX"%s: enabling failed",
                     thermal_sensor_generic_get_name(self));
            goto EXIT;
        }

        if( !self->sg_temp_cb(self->sg_temp_path, &temp) ) {
            dsme_log(LOG_WARNING, PFIX"%s: reading still failed",
                     thermal_sensor_generic_get_name(self));
            goto EXIT;
        }

        dsme_log(LOG_DEBUG, PFIX"%s: succesfully re-enabled",
                 thermal_sensor_generic_get_name(self));
    }

    temp += self->sg_temp_offs;

    for( int i = 0; i < THERMAL_STATUS_COUNT; ++i ) {
        if( temp >= self->sg_level[i].sl_mintemp )
            status = i;
    }

    ack = true;

EXIT:
    self->sg_temp   = temp;
    self->sg_status = status;

    return ack;
}

/** Set path of sensor object sensor value file
 *
 * @param self  sensor object
 * @param path  file from which temperature can be read
 */
static void
thermal_sensor_generic_set_temp_path(thermal_sensor_generic_t *self,
                                     const char *path)
{
    tsg_util_set_string(&self->sg_temp_path, path);
    self->sg_is_meta = false;
}

/** Set sensor object temperature read function
 *
 * @param self  sensor object
 * @param cb    callback that can read and cache temperature
 */
static void
thermal_sensor_generic_set_temp_func(thermal_sensor_generic_t *self,
                                     sg_temp_fn cb)
{
    self->sg_is_meta = false;
    self->sg_temp_cb = cb;
}

/** Set sensor object sensor enable/disable controls
 *
 * @param self     sensor object
 * @param path     file that can be used to enable/disable sensor
 * @param enable   string to write when enabling sensor
 * @param disable  string to write when disabling sensor
 */
static void
thermal_sensor_generic_set_mode_control(thermal_sensor_generic_t *self,
                                        const char *path,
                                        const char *enable,
                                        const char *disable)
{
    tsg_util_set_string(&self->sg_mode_path,    path);
    tsg_util_set_string(&self->sg_mode_enable,  enable);
    tsg_util_set_string(&self->sg_mode_disable, disable);
}

/** Set sensor object thermal limits
 *
 * @param self     sensor object
 * @param status   thermal status to configure
 * @param mintem   minimum temperature needed for this status
 * @param minwait  minimum polling delay while in this status
 * @param maxwait  maxnimum polling delay while in this status
 */
static void
thermal_sensor_generic_set_limit(thermal_sensor_generic_t *self,
                                 THERMAL_STATUS status,
                                 int mintemp, int minwait, int maxwait)
{
    if( status >= 0 && status < THERMAL_STATUS_COUNT ) {
        self->sg_level[status].sl_mintemp = mintemp;
        self->sg_level[status].sl_minwait = minwait;
        self->sg_level[status].sl_maxwait = maxwait;
    }
}

/** Set sensor object as meta sensor depending on another one
 *
 * @param self     sensor object
 */
static void
thermal_sensor_generic_set_depends_on(thermal_sensor_generic_t *self,
                                      const char *sensor_name)
{
    tsg_util_set_string(&self->sg_temp_path, sensor_name);
    self->sg_is_meta = true;
    self->sg_temp_cb = tsg_util_read_other;
}

/** Set sensor object temperature correction offset
 *
 * Needed for configuring meta sensors like surface
 * temperature that is estimated from battery temperature
 *
 * @param self     sensor object
 * @param offs     correction offset [C]
 */
static void
thermal_sensor_generic_set_temp_offs(thermal_sensor_generic_t *self, int offs)
{
    self->sg_temp_offs = offs;
}

/* ========================================================================= *
 * HOOKS_FOR_THERMAL_OBJECT
 * ========================================================================= */

/** Hook functions for interfacing via thermal object API */
static const thermal_sensor_vtab_t thermal_sensor_generic_vtab =
{
    .tsv_delete_cb         = thermal_sensor_generic_delete_cb,
    .tsv_get_name_cb       = thermal_sensor_generic_get_name_cb,
    .tsv_get_depends_on_cb = thermal_sensor_generic_get_depends_on_cb,
    .tsv_read_sensor_cb    = thermal_sensor_generic_read_sensor_cb,
    .tsv_get_status_cb     = thermal_sensor_generic_get_status_cb,
    .tsv_get_poll_delay_cb = thermal_sensor_generic_get_poll_delay_cb
};

/** Get sensor object from thermal object
 *
 * @param object thermal object
 *
 * @return sensor object, or NULL
 */
static thermal_sensor_generic_t *
thermal_sensor_generic_from_object(const thermal_object_t *object)
{
    thermal_sensor_generic_t *self = 0;

    if( thermal_object_has_sensor_vtab(object, &thermal_sensor_generic_vtab) )
        self = thermal_object_get_sensor_data(object);

    return self;
}

/** Hook function for deleting sensor object
 *
 * @param object thermal object, or NULL
 */
static void
thermal_sensor_generic_delete_cb(thermal_object_t *object)
{
    thermal_sensor_generic_t *self =
        thermal_sensor_generic_from_object(object);

    thermal_sensor_generic_delete(self);
}

/** Hook function for getting sensor object name
 *
 * @param object thermal object
 *
 * @return sensor name
 */
static const char *
thermal_sensor_generic_get_name_cb(const thermal_object_t *object)
{
    thermal_sensor_generic_t *self =
        thermal_sensor_generic_from_object(object);

    return thermal_sensor_generic_get_name(self);
}

/** Hook function for getting sensor object dependency name
 *
 * @param object thermal object
 *
 * @return sensor name
 */
static const char *
thermal_sensor_generic_get_depends_on_cb(const thermal_object_t *object)
{
    thermal_sensor_generic_t *self =
        thermal_sensor_generic_from_object(object);

    return thermal_sensor_generic_get_depends_on(self);
}

/** Hook function for getting sensor object status
 *
 * @param object  thermal object
 * @param status  where to store thermal status
 * @param temp    where to store temperature value
 *
 * @return true if values were obtained, false otherwise
 */
static bool
thermal_sensor_generic_get_status_cb(const thermal_object_t *object,
                             THERMAL_STATUS *status, int *temp)
{
    thermal_sensor_generic_t *self =
        thermal_sensor_generic_from_object(object);

    return thermal_sensor_generic_get_status(self, temp, status);
}

/** Hook function for getting sensor object poll delay
 *
 * @param object   thermal object
 * @param minwait  where to store minimum wait delay [s]
 * @param maxwait  where to store maximum wait delay [s]
 *
 * @return true if values were obtained, false otherwise
 */
static bool
thermal_sensor_generic_get_poll_delay_cb(const thermal_object_t *object,
                              int *minwait, int *maxwait)
{
    thermal_sensor_generic_t *self =
        thermal_sensor_generic_from_object(object);

    return thermal_sensor_generic_get_poll_delay(self, minwait, maxwait);
}

/** Idle callback for notifying thermal object
 *
 * @param aptr  thermal object as void pointer
 *
 * @return FALSE to stop timer from repeating
 */
static gboolean
thermal_sensor_generic_sensor_notify_cb(gpointer aptr)
{
    thermal_object_t *object = aptr;

    thermal_sensor_generic_t *self =
        thermal_sensor_generic_from_object(object);

    if( !self )
        goto EXIT;

    if( !self->sg_notify_id )
        goto EXIT;

    self->sg_notify_id = 0;

    thermal_object_handle_update(object);

EXIT:
    return FALSE;
}

/** Hook function for getting sensor object poll delay
 *
 * Thermal object assumes there is asynchronous notification
 * when the value is actually available.
 *
 * In theory everything should work even if we would do
 * the update notification before returning to the caller,
 * but in order to minimize chances of deep recursion due
 * to meta sensor handling, the notification is sent via
 * idle callback.
 *
 * @param object   thermal object
 *
 * @return true if reading could be initiated, false otherwise
 */
static bool
thermal_sensor_generic_read_sensor_cb(thermal_object_t *object)
{
    bool ack = false;

    thermal_sensor_generic_t *self =
        thermal_sensor_generic_from_object(object);

    if( !thermal_sensor_generic_read_sensor(self) )
        goto EXIT;

#if 0
    thermal_object_handle_update(object);
#else
    if( !self->sg_notify_id ) {
        self->sg_notify_id =
            g_idle_add(thermal_sensor_generic_sensor_notify_cb, object);
    }
#endif
    ack = true;

EXIT:
    return ack;
}

/* ========================================================================= *
 * CONFIGURATION_FILE
 * ========================================================================= */

/** Locate thermal object from list by name
 *
 * @param list  head of linked list
 * @param name  name of the sensor to find
 *
 * @return thermal object, or NULL if not found
 */
static thermal_object_t *
tsg_objects_get_object(GSList *list, const char *name)
{
    for( GSList *item = list; item; item = item->next ) {
        thermal_object_t *object = item->data;

        if( !object )
            continue;

        if( thermal_object_has_name(object, name) )
            return object;
    }

    return 0;
}

/** Locate/add thermal object in list of sensors
 *
 * @param list  pointer to the head of linked list
 * @param name  name of the sensor to find
 *
 * @return thermal object
 */
static thermal_object_t *
tsg_objects_add_object(GSList **list, const char *name)
{
    thermal_object_t *object = tsg_objects_get_object(*list, name);

    if( !object ) {
        thermal_sensor_generic_t *sensor = thermal_sensor_generic_create(name);
        object = thermal_object_create(&thermal_sensor_generic_vtab,
                                       sensor);
        *list = g_slist_prepend(*list, object);
    }

    return object;
}

/** Locate/add sensor object in list of sensors
 *
 * @param list  pointer to the head of linked list
 * @param name  name of the sensor to find
 *
 * @return sensor object
 */
static thermal_sensor_generic_t *
tsg_objects_add_sensor(GSList **list, const char *name)
{
    thermal_object_t *object = tsg_objects_add_object(list, name);
    return thermal_sensor_generic_from_object(object);
}

/** Validate and register a list of thermal objects
 *
 * @param list  pointer to the head of a linked list
 */
static void
tsg_objects_register_all(GSList **list)
{
    for( GSList *item = *list; item; item = item->next ) {
        thermal_object_t *object = item->data;

        if( !object )
            continue;

        thermal_sensor_generic_t *sensor =
            thermal_sensor_generic_from_object(object);

        if( !sensor )
            continue;

        if( !thermal_sensor_generic_is_valid(sensor) ) {
            dsme_log(LOG_ERR, PFIX"%s: %s",
                     thermal_sensor_generic_get_name(sensor),
                     "sensor config is not valid");
        }
        else if( !thermal_sensor_generic_enable_sensor(sensor, true) ) {
            dsme_log(LOG_ERR, PFIX"%s: %s",
                     thermal_sensor_generic_get_name(sensor),
                     "sensor could not be enabled");
        }
        else if( !thermal_sensor_generic_read_sensor(sensor) ) {
            dsme_log(LOG_ERR, PFIX"%s: %s",
                     thermal_sensor_generic_get_name(sensor),
                     "sensor could not be read");
        }
        else {
            thermal_manager_register_object(object);
            continue;
        }

        item->data = 0;
        thermal_object_delete(object);
    }
}

/** Convert thermal limit name thermal status enum value
 *
 * @param key key string
 *
 * @return thermal status, or -1 on failure
 */
static THERMAL_STATUS
tsg_objects_parse_level(const char *key)
{
    static const struct
    {
        const char *key;
        int         val;
    } lut[] = {
        { CONFIG_KW_LOW,     THERMAL_STATUS_LOW     },
        { CONFIG_KW_NORMAL,  THERMAL_STATUS_NORMAL  },
        { CONFIG_KW_WARNING, THERMAL_STATUS_WARNING },
        { CONFIG_KW_ALERT,   THERMAL_STATUS_ALERT   },
        { CONFIG_KW_FATAL,   THERMAL_STATUS_FATAL   },
        { CONFIG_KW_INVALID, THERMAL_STATUS_INVALID },
        { 0,         -1 }
    };

    THERMAL_STATUS status = -1;

    for( int i = 0; ; ++i ) {
        if( lut[i].key && strcmp(lut[i].key, key) )
            continue;

        status = lut[i].val;
        break;
    }

    return status;
}

/** Read configuration file and generate thermal objects
 *
 * The configuration file format is described in
 *   doc/thermal_sensor_config_files.txt
 *
 * @param list    pointer to the head of a linked list
 * @param config  path to configuration file
 */
static void
tsg_objects_read_config(GSList **list, const char *config)
{
    char   *buff = 0;
    size_t  size = 0;
    FILE   *file = 0;

    file = fopen(config, "r");
    if( !file )
        goto EXIT;

    thermal_sensor_generic_t *sensor = 0;

    for( int line = 1; ; ++line ) {
        int rc = getline(&buff, &size, file);
        if( rc < 0 )
            break;

        char *pos = buff;
        char *key = tsg_util_slice_key(&pos);

        /* Ignore empty and comment lines */
        if( *key == 0 || *key == '#' ) {
            continue;
        }

        /* Parse config entry */
        if( !strcmp(key, CONFIG_KW_NAME) ) {
            // Name: <sensor_name>
            sensor = tsg_objects_add_sensor(list, tsg_util_slice_str(&pos));
        }
        else if( !sensor ) {
            dsme_log(LOG_ERR, PFIX"%s:%d: stray configuration item: %s",
                     config, line, key);
        }
        else if( !strcmp(key, CONFIG_KW_TEMP) ) {
            // Temp: <temperature_read_path> <unit> [temperature_offset]
            char *path = tsg_util_slice_str(&pos);
            char *type = tsg_util_slice_str(&pos);
            int   offs = tsg_util_slice_int(&pos);

            thermal_sensor_generic_set_temp_path(sensor, path);
            thermal_sensor_generic_set_temp_offs(sensor, offs);

            if( !strcmp(type, "C") ) {
                thermal_sensor_generic_set_temp_func(sensor,
                                                     tsg_util_read_temp_C);
            }
            else if( !strcmp(type, "dC") ) {
                thermal_sensor_generic_set_temp_func(sensor,
                                                     tsg_util_read_temp_dC);
            }
            else if( !strcmp(type, "mC") ) {
                thermal_sensor_generic_set_temp_func(sensor,
                                                     tsg_util_read_temp_mC);
            }
            else {
                dsme_log(LOG_ERR, PFIX"%s:%d: unknown/missing temp type: %s",
                         config, line, type);
                thermal_sensor_generic_set_temp_func(sensor, 0);
            }
        }
        else if( !strcmp(key, CONFIG_KW_META) ) {
            // Meta: <sensor_name> [temperature_offset]
            char *name = tsg_util_slice_str(&pos);
            int   offs = tsg_util_slice_int(&pos);
            thermal_sensor_generic_set_depends_on(sensor, name);
            thermal_sensor_generic_set_temp_offs(sensor, offs);
        }
        else if( !strcmp(key, CONFIG_KW_MODE) ) {
            // Mode: <mode_write_path> <enable_string> <disable_string>
            char *path    = tsg_util_slice_str(&pos);
            char *enable  = tsg_util_slice_str(&pos);
            char *disable = tsg_util_slice_str(&pos);
            thermal_sensor_generic_set_mode_control(sensor,
                                                    path, enable, disable);
        }
        else if( (rc = tsg_objects_parse_level(key)) != -1 ) {
            // Low|Normal|...|Fatal|Invalid: <mintemp> <minwait> <maxwait>
            int mintemp = tsg_util_slice_int(&pos);
            int minwait = tsg_util_slice_int(&pos);
            int maxwait = tsg_util_slice_int(&pos);
            thermal_sensor_generic_set_limit(sensor, rc,
                                             mintemp, minwait, maxwait);
        }
        else {
            dsme_log(LOG_ERR, PFIX"%s:%d: unknown item: %s",
                     config, line, key);
            continue;
        }

        /* Check if line contains unparsed elements */
        pos = tsg_util_slice_token(pos, 0, 0);
        if( *pos != 0 && *pos != '#' ) {
            dsme_log(LOG_WARNING, PFIX"%s:%d: excess elements: %s",
                     config, line, pos);
        }
    }

EXIT:
    free(buff);

    if( file ) fclose(file);
}

/** Cleanup thermal objects held in a linked list
 *
 * @param list    head of linked list
 */
static void
tsg_objects_quit(GSList **list)
{
    for( GSList *item = *list; item; item = item->next ) {
        thermal_object_t *object = item->data;

        if( !object )
            continue;

        item->data = 0;
        thermal_object_delete(object);
    }

    g_slist_free(*list), *list = 0;

}

/** Create linked list of thermal objects based on config files
 *
 * @param list    head of linked list
 */
static void
tsg_objects_init(GSList **list)
{
    static const char pat[] = "/etc/dsme/thermal_sensor_*.conf";

    glob_t gl = {};

    if( glob(pat, GLOB_ERR, 0, &gl) != 0 ) {
        dsme_log(LOG_WARNING, PFIX"No thermal config files found");
        goto EXIT;
    }

    for( int i = 0; i < gl.gl_pathc; ++i )
        tsg_objects_read_config(list, gl.gl_pathv[i]);

    *list = g_slist_reverse(*list);

    tsg_objects_register_all(list);

EXIT:
    globfree(&gl);
}

/* ========================================================================= *
 * DSME_PLUGIN_GLUE
 * ========================================================================= */

/** Linked list of thermal objects created by this module */
static GSList *objects_list = 0;

/** Handler for connected to D-Bus system bus event
 *
 * The dbus connect serves just as a suitable point in time when
 * dsme startup has progressed far enough to allow registering
 * of thermal objects.
 */
DSME_HANDLER(DSM_MSGTYPE_DBUS_CONNECT, client, msg)
{
    dsme_log(LOG_DEBUG, PFIX"DBUS_CONNECT");
    tsg_objects_init(&objects_list);
}

/** Handler for disconnected to D-Bus system bus event
 *
 * As it stands now, registered thermal objects can cause dbus
 * traffic and thus must be unregistered if dbus connection
 * is lost.
 */
DSME_HANDLER(DSM_MSGTYPE_DBUS_DISCONNECT, client, msg)
{
    dsme_log(LOG_DEBUG, PFIX"DBUS_DISCONNECT");
    tsg_objects_quit(&objects_list);
}

/** Array of DSME event handlers implemented by this plugin */
module_fn_info_t message_handlers[] =
{
    DSME_HANDLER_BINDING(DSM_MSGTYPE_DBUS_CONNECT),
    DSME_HANDLER_BINDING(DSM_MSGTYPE_DBUS_DISCONNECT),
    { 0 }
};

/** Init hook that DSME plugins need to implement
 *
 * @param handle (unused) DSME plugin handle
 */
void
module_init(module_t *handle)
{
    (void)handle;

    dsme_log(LOG_DEBUG, PFIX"loaded");
}

/** Exit hook that DSME plugins need to implement
 */
void
module_fini(void)
{
    tsg_objects_quit(&objects_list);
    dsme_log(LOG_DEBUG, PFIX"unloaded");
}
