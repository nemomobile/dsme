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

#include <dsme/protocol.h>

#include "malf.h"
#include "../include/dsme/modules.h"
#include "../include/dsme/logging.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <glib.h>
#include <ctype.h>
#include <errno.h>


#define DSME_STATIC_STRLEN(s) (sizeof(s) - 1)


// TODO: try to find a header that #defines NETLINK_VALIDATOR
//       and possibly the right group mask to use
#ifndef NETLINK_VALIDATOR
#define NETLINK_VALIDATOR 25
#endif
#ifndef VALIDATOR_MAX_PAYLOAD
#define VALIDATOR_MAX_PAYLOAD 4096
#endif

#define DSME_CONFIG_VALIDATED_PATH   "/etc/init.conf"
#define DSME_CONFIG_VALIDATED_PREFIX "mandatorybinary "


static void stop_listening_to_validator(void);
static bool read_mandatory_file_list(const char* config_path, GSList** files);
static bool is_in_list(const char* file, GSList* list);
static bool is_basename_in_list(const char* file, GSList* list);


static int         validator_fd = -1; // TODO: make local in start_listening
static GIOChannel* channel      = 0;

static bool        got_mandatory_files = false;
static GSList*     mandatory_files     = 0;

static const int   VREASON_OK          = 0; // validation ok
static const int   VREASON_HLIST       = 2; // reference value not found

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
                                    int*        vreason,
                                    char**      component,
                                    char**      details)
{
    *vreason   = VREASON_OK;
    *component = 0;
    *details   = 0;

    const char* p = msg;
    char*       key;
    char*       text;

    // skip leading space
    while (p && *p && isspace(*p)) {
        p++;
    }

    while (p && *p && parse_validator_line(&p, &key, &text)) {
        if (strcmp(key, "Fail") == 0) {
            *vreason = atoi(text);
            free(text);
        } else if (strcmp(key, "Process") == 0) {
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

static bool check_security_malf(int vreason, char* component, char* details)
{
    bool success = true;

    // no list of mandatory files => MALF
    if (!got_mandatory_files) {
        success = false;
        goto out;
    }

    // a list of mandatory files exists; check against it
    if (is_in_list(details, mandatory_files) ||
        is_basename_in_list(component, mandatory_files)) {

        // this file was on the list => MALF if the validation failed
        // because of anything else than a missing reference hash
        if (vreason != VREASON_HLIST) {
            success = false;
        }
    }

out:
    return success;
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

            int   vreason;
            char* component;
            char* details;
            parse_validator_message(NLMSG_DATA(nlh), &vreason, &component, &details);

            if (!check_security_malf(vreason, component, details)) {
                dsme_log(LOG_CRIT,
                         "Security MALF: %i %s %s",
                         vreason,
                         component,
                         details);

                go_to_malf(component, details);
                // NOTE: we leak component and details;
                // it is OK because we are entering MALF anyway
            } else {
                // the file was not on the list => no MALF
                dsme_log(LOG_INFO, "OK, not a mandatory file: %s", details);
                free(component);
                free(details);
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

static bool is_mandatory(const char* line, char** path)
{
    const char mandatory_prefix[] = DSME_CONFIG_VALIDATED_PREFIX;
    bool mandatory = false;

    if (strncmp(line,
                mandatory_prefix,
                DSME_STATIC_STRLEN(mandatory_prefix)) == 0)
    {
        mandatory = true;
        *path     = strdup(line + DSME_STATIC_STRLEN(mandatory_prefix));
    }

    return mandatory;
}

static bool read_mandatory_file_list(const char* config_path, GSList** files)
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
                line[length - 1] = '\0';
                --length;
            }
        }

        // add line to config
        char* path;
        if (files && is_mandatory(line, &path)) {
            *files = g_slist_append(*files, path);
        }
        free(line);
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

    if (!read_mandatory_file_list(DSME_CONFIG_VALIDATED_PATH, &mandatory_files))
    {
        dsme_log(LOG_WARNING, "failed to load the list of mandatory files");
    } else {
        got_mandatory_files = true;

        if (!start_listening_to_validator()) {
            dsme_log(LOG_CRIT, "failed to start listening to Validator");
            go_to_malf("dsme", "failed to start listening to Validator");
        }
    }
}

void module_fini(void)
{
    stop_listening_to_validator();
    g_slist_free(mandatory_files);

    dsme_log(LOG_DEBUG, "validatorlistener.so unloaded");
}
