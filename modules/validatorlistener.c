/**
   @file validatorlistener.c

   Listen to Validator messages from kernel during startup.
   Validator messages are received via netlink socket.
   Stop listening when init signals that we are about to
   launch 3rd party daemons.
   <p>
   Copyright (C) 2011 Nokia Corporation.

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

// to stop listening to Validator:
// dbus-send --system --type=signal /com/nokia/startup/signal com.nokia.startup.signal.base_boot_done

#include <dsme/protocol.h>

#include "validatorlistener.h"
#include "dsme_dbus.h"
#include "dbusproxy.h"

#include "malf.h"
#include "dsme/modules.h"
#include "dsme/logging.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <glib.h>
#include <ctype.h>
#include <errno.h>


// TODO: try to find a header that #defines NETLINK_VALIDATOR
//       and possibly the right group mask to use
#ifndef NETLINK_VALIDATOR
#define NETLINK_VALIDATOR 25
#endif
#ifndef VALIDATOR_MAX_PAYLOAD
#define VALIDATOR_MAX_PAYLOAD 4096
#endif

#define DSME_CONFIG_VALIDATED_PATH "/var/lib/dsme/mandatory_files" // TODO


static void stop_listening_to_validator(void);
static bool read_file_to_list(const char* config_path, GSList** files);
static bool is_in_list(const char* file, GSList* list);
static bool is_basename_in_list(const char* file, GSList* list);


static int         validator_fd = -1; // TODO: make local in start_listening
static GIOChannel* channel         = 0;
static GSList*     mandatory_files = 0;


static void go_to_malf(const char* component, const char* details)
{
    DSM_MSGTYPE_ENTER_MALF malf = DSME_MSG_INIT(DSM_MSGTYPE_ENTER_MALF);
    malf.reason          = DSME_MALF_SECURITY;
    malf.component       = component;

    broadcast_internally_with_extra(&malf, strlen(details) + 1, details);
}


// parse a line of format "<key>: <text>"
static bool parse_validator_line(const char** msg, char** key, char** text)
{
    bool parsed = false;

    const char* p;
    if ((p = strchr(*msg, ':'))) {
        // got the key
        *key = strndup(*msg, p - *msg);

        // skip whitespace
        do {
            ++p;
        } while (*p && *p != '\n' && isblank((unsigned char)*p));

        // got the beginning of text; now determine where it ends
        const char* t = p;
        while (*p && *p != '\n') {
            ++p;
        }
        *text = strndup(t, p - t);

        // move to the next line if necessary, and save the parsing point
        if (*p == '\n') {
            ++p;
        }
        *msg = p;

        parsed = true;
    }

    return parsed;
}

static void parse_validator_message(const char* msg,
                                    char**      component,
                                    char**      details)
{
    *component = 0;
    *details   = 0;

    const char* p = msg;
    char*       key;
    char*       text;
    while (p && *p && parse_validator_line(&p, &key, &text)) {
        if (strcmp(key, "Process") == 0) {
            free(*component);
            *component = text;
        } else if (strcmp(key, "File") == 0) {
            free(*details);
            *details = text;
        } else {
            free(text);
        }
        free(key);
    }

    if (!*component) {
        *component = strdup("(unknown)");
    }
    if (!*details) {
        *details = strdup("(unknown)");
    }
}

static gboolean handle_validator_message(GIOChannel*  source,
                                         GIOCondition condition,
                                         gpointer     data)
{
    dsme_log(LOG_DEBUG, "Activity on Validator socket");

    bool keep_listening = true;

    if (condition & G_IO_IN) {

        struct sockaddr_nl addr;
        memset(&addr, 0, sizeof(addr));

        static struct nlmsghdr* nlh = 0;
        if (!nlh) {
            nlh = (struct nlmsghdr*)malloc(NLMSG_SPACE(VALIDATOR_MAX_PAYLOAD));
            // TODO: check for NULL & free when done
        }
        memset(nlh, 0, NLMSG_SPACE(VALIDATOR_MAX_PAYLOAD));

        struct iovec iov;
        memset(&iov, 0, sizeof(iov));
        iov.iov_base = (void*)nlh;
        iov.iov_len  = NLMSG_SPACE(VALIDATOR_MAX_PAYLOAD);

        struct msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_name    = (void*)&addr;
        msg.msg_namelen = sizeof(addr);
        msg.msg_iov     = &iov;
        msg.msg_iovlen  = 1;


        int r = TEMP_FAILURE_RETRY(recvmsg(validator_fd, &msg, 0));

        if (r == 0 || r == -1) {
            dsme_log(LOG_ERR, "Error receiving Validator message");
            keep_listening = false;
        } else {
            dsme_log(LOG_CRIT,
                     "Got Validator message [%s]",
                     (char*)NLMSG_DATA(nlh));

            // TODO: check that the message is from the kernel

            char* component;
            char* details;
            parse_validator_message(NLMSG_DATA(nlh), &component, &details);

            // if a list of mandatory files exists; check against it
            if (!mandatory_files                     ||
                is_in_list(details, mandatory_files) ||
                is_basename_in_list(component, mandatory_files))
            {
                // either there was no list of mandatory files,
                // or this file was on the list => MALF

                dsme_log(LOG_CRIT,
                         "Security MALF: %s %s",
                         component,
                         details);

                go_to_malf(component, details);
                // NOTE: we leak component and details;
                // it is OK because we are entering MALF anyway
            } else {
                // the file was not on the list => no MALF
                dsme_log(LOG_INFO, "OK, not a mandatory file: %s", details);
            }
        }
    }
    if (condition & (G_IO_ERR | G_IO_HUP | G_IO_NVAL)) {
        dsme_log(LOG_ERR, "ERR, HUP or NVAL on Validator socket");
        keep_listening = false;
    }

    if (!keep_listening) {
        stop_listening_to_validator();
    }

    return keep_listening;
}


static bool start_listening_to_validator(void)
{
    validator_fd = socket(PF_NETLINK, SOCK_RAW, NETLINK_VALIDATOR);
    if (validator_fd == -1) {
        dsme_log(LOG_ERR, "Validator socket: %s", strerror(errno));
        goto fail;
    }

    struct sockaddr_nl addr;
    memset(&addr, 0, sizeof(addr));
    addr.nl_family = AF_NETLINK;
    addr.nl_pid    = getpid();
    addr.nl_groups = 1; // TODO: magic number: group mask for Validator

    if (bind(validator_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        dsme_log(LOG_ERR, "Validator socket bind: %s", strerror(errno));
        goto close_and_fail;
    }

    guint watch = 0;

    if (!(channel = g_io_channel_unix_new(validator_fd))) {
        goto close_and_fail;
    }
    g_io_channel_set_encoding(channel, 0, 0);
    g_io_channel_set_buffered(channel, FALSE);
    g_io_channel_set_close_on_unref(channel, TRUE);

    watch = g_io_add_watch(channel,
                           (G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL),
                           handle_validator_message,
                           0);
    g_io_channel_unref(channel);
    if (!watch) {
        goto fail;
    }

    return true;


close_and_fail:
    close(validator_fd);
    validator_fd = -1;

fail:
    return false;
}

static void stop_listening_to_validator(void)
{
    if (channel) {
        g_io_channel_shutdown(channel, FALSE, 0);
        channel = 0;
    }
}


// TODO: move init_done_ind -> DSM_MSGTYPE_INIT_DONE code to diskmonitor
static void init_done_ind(const DsmeDbusMessage* ind)
{
    dsme_log(LOG_DEBUG, "base_boot_done");
    DSM_MSGTYPE_INIT_DONE msg = DSME_MSG_INIT(DSM_MSGTYPE_INIT_DONE);
    broadcast_internally(&msg);
}

static bool bound = false;

static const dsme_dbus_signal_binding_t signals[] = {
    { init_done_ind, "com.nokia.startup.signal", "base_boot_done" },
    { 0, 0 }
};


DSME_HANDLER(DSM_MSGTYPE_DBUS_CONNECT, client, msg)
{
  dsme_log(LOG_DEBUG, "validatorlistener: DBUS_CONNECT");
  dsme_dbus_bind_signals(&bound, signals);
}

DSME_HANDLER(DSM_MSGTYPE_DBUS_DISCONNECT, client, msg)
{
  dsme_log(LOG_DEBUG, "validatorlistener: DBUS_DISCONNECT");
  dsme_dbus_unbind_signals(&bound, signals);
}

module_fn_info_t message_handlers[] = {
  DSME_HANDLER_BINDING(DSM_MSGTYPE_DBUS_CONNECT),
  DSME_HANDLER_BINDING(DSM_MSGTYPE_DBUS_DISCONNECT),
  { 0 }
};


static bool read_file_to_list(const char* config_path, GSList** files)
{
    bool  have_the_list = false;
    FILE* config;

    if ((config = fopen(config_path, "r")) == 0) {
        // could not open list of files to validate; MALF
        dsme_log(LOG_WARNING,
                 "Could not open mandatory file list '%s': %m",
                 config_path);
        goto done;
    }

    char*   line   = 0;
    size_t  size   = 0;
    ssize_t length;

    while ((length = getline(&line, &size, config)) != -1) {
        // remove trailing newline
        if (length > 0) {
            if (line[length - 1] == '\n') {
                --length;
            }
        }

        // add line to config
        if (files) {
            *files = g_slist_append(*files, line);
        }

        line = 0;
        size = 0;
    }
    have_the_list = true;

    fclose(config);

done:
    return have_the_list;
}

static bool is_in_list(const char* file, GSList* list)
{
    GSList* node;

    for (node = list; node != 0; node = g_slist_next(node)) {
        if (strcmp(file, node->data) == 0) {
            break;
        }
    }

    return node != 0;
}

static bool is_basename_in_list(const char* file, GSList* list)
{
    GSList* node;

    for (node = list; node != 0; node = g_slist_next(node)) {
        if (strcmp(file, basename(node->data)) == 0) {
            break;
        }
    }

    return node != 0;
}


void module_init(module_t* handle)
{
    dsme_log(LOG_DEBUG, "validatorlistener.so loaded");

    if (!read_file_to_list(DSME_CONFIG_VALIDATED_PATH, &mandatory_files))
    {
        dsme_log(LOG_WARNING, "failed to load the list of mandatory files");
    } else if (!start_listening_to_validator()) {
        dsme_log(LOG_CRIT, "failed to start listening to Validator");
        // TODO: go_to_malf();
    }
}

void module_fini(void)
{
    stop_listening_to_validator();
    g_slist_free(mandatory_files);

    dsme_log(LOG_DEBUG, "validatorlistener.so unloaded");
}
