/**
   @file dsmetool.c

   Dsmetool can be used to send commands to DSME.
   <p>
   Copyright (C) 2004-2010 Nokia Corporation.

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

#define _GNU_SOURCE

#include "../modules/dbusproxy.h"
#include "../modules/state-internal.h"
#include "../include/dsme/logging.h"
#include <dsme/state.h>
#include <dsme/protocol.h>

#include <stdlib.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>

#include <pwd.h>
#include <grp.h>

#define STRINGIFY(x)  STRINGIFY2(x)
#define STRINGIFY2(x) #x


static dsmesock_connection_t* conn;


static void usage(const char* name)
{
    printf("USAGE: %s <options>\n", name);
    printf(
"Note that the <cmd> should include absolute path.\n"
"  -d --start-dbus                 Start DSME's D-Bus services\n"
#if 0 // TODO
"  -s --stop-dbus                  Stop DSME's D-Bus services\n"
#endif
"  -b --reboot                     Reboot the device\n"
"  -v --version                    Print the versions of DSME and dsmetool\n"
"  -t --telinit <runlevel name>    Change runlevel\n"
"  -l --loglevel <0..7>            Change DSME's logging verbosity\n"
"  -h --help                       Print usage\n");
}

static void connect_to_dsme(void)
{
    conn = dsmesock_connect();
    if (conn == 0) {
        perror("dsmesock_connect");
        exit(EXIT_FAILURE);
    }
}

static void disconnect_from_dsme(void)
{
    dsmesock_close(conn);
}

static void send_to_dsme(const void* msg)
{
    if (dsmesock_send(conn, msg) == -1) {
        perror("dsmesock_send");
        exit(EXIT_FAILURE);
    }
}

static void send_to_dsme_with_string(const void* msg, const char* s)
{
    if (dsmesock_send_with_extra(conn, msg, strlen(s) + 1, s) == -1) {
        perror("dsmesock_send_with_extra");
        exit(EXIT_FAILURE);
    }
}


static int get_version(void)
{
    DSM_MSGTYPE_GET_VERSION   req_msg =
          DSME_MSG_INIT(DSM_MSGTYPE_GET_VERSION);
    void*                     p;
    DSM_MSGTYPE_DSME_VERSION* retmsg = NULL;
    fd_set                    rfds;
    int                       ret = -1;

    printf("dsmetool version: %s\n", STRINGIFY(PRG_VERSION));

    connect_to_dsme();

    send_to_dsme(&req_msg);

    while (true) {
        FD_ZERO(&rfds);
        FD_SET(conn->fd, &rfds);
        struct timeval tv;

        tv.tv_sec = 5;
        tv.tv_usec = 0;

        ret = select(conn->fd+1, &rfds, NULL, NULL, &tv);
        if (ret == -1) {
            fprintf(stderr, "Error in select()\n");
            return -1;
        }
        if (ret == 0) {
            fprintf(stderr, "Timeout when getting the DSME version\n");
            disconnect_from_dsme();
            return -1;
        }

        p = dsmesock_receive(conn);

        if ((retmsg = DSMEMSG_CAST(DSM_MSGTYPE_DSME_VERSION, p)) == 0) {
            fprintf(stderr, "Received invalid message\n");
            free(p);
            continue;
        }
        break;
    }

    char* version = (char*)DSMEMSG_EXTRA(retmsg);
    if (version != 0) {
        version[DSMEMSG_EXTRA_SIZE(retmsg) - 1] = '\0';
        printf("DSME version: %s\n", version);
    }

    free(p);
    disconnect_from_dsme();

    return 0;
}

static int send_dbus_service_start_request()
{
    DSM_MSGTYPE_DBUS_CONNECT msg = DSME_MSG_INIT(DSM_MSGTYPE_DBUS_CONNECT);

    connect_to_dsme();
    send_to_dsme(&msg);
    disconnect_from_dsme();

    return EXIT_SUCCESS;
}

static int send_dbus_service_stop_request()
{
    DSM_MSGTYPE_DBUS_DISCONNECT msg =
      DSME_MSG_INIT(DSM_MSGTYPE_DBUS_DISCONNECT);

    connect_to_dsme();
    send_to_dsme(&msg);
    disconnect_from_dsme();

    return EXIT_SUCCESS;
}

static int send_reboot_request()
{
    DSM_MSGTYPE_REBOOT_REQ msg = DSME_MSG_INIT(DSM_MSGTYPE_REBOOT_REQ);

    connect_to_dsme();
    send_to_dsme(&msg);
    disconnect_from_dsme();

    return EXIT_SUCCESS;
}

static int send_ta_test_request()
{
    DSM_MSGTYPE_SET_TA_TEST_MODE msg =
        DSME_MSG_INIT(DSM_MSGTYPE_SET_TA_TEST_MODE);

    connect_to_dsme();
    send_to_dsme(&msg);
    disconnect_from_dsme();

    return EXIT_SUCCESS;
}

static int telinit(const char* runlevel)
{
    DSM_MSGTYPE_TELINIT msg = DSME_MSG_INIT(DSM_MSGTYPE_TELINIT);

    connect_to_dsme();
    send_to_dsme_with_string(&msg, runlevel);
    // TODO: wait for OK/NOK from dsme
    disconnect_from_dsme();

    return EXIT_SUCCESS;
}

static int loglevel(unsigned level)
{
    DSM_MSGTYPE_SET_LOGGING_VERBOSITY msg =
        DSME_MSG_INIT(DSM_MSGTYPE_SET_LOGGING_VERBOSITY);
    msg.verbosity = level;

    connect_to_dsme();
    send_to_dsme(&msg);
    disconnect_from_dsme();

    return EXIT_SUCCESS;
}

int main(int argc, char* argv[])
{
    const char* program_name  = argv[0];
    int         next_option;
    int         retval        = EXIT_SUCCESS;
    const char* short_options = "hdsbvat:l:";
    const struct option long_options[] = {
        {"help",       no_argument,       NULL, 'h'},
        {"start-dbus", no_argument,       NULL, 'd'},
        {"stop-dbus",  no_argument,       NULL, 's'},
        {"reboot",     no_argument,       NULL, 'b'},
        {"version",    no_argument,       NULL, 'v'},
        {"ta-test",    no_argument,       NULL, 'a'},
        {"telinit",    required_argument, NULL, 't'},
        {"loglevel",   required_argument, NULL, 'l'},
        {0, 0, 0, 0}
    };

    do {
        next_option = getopt_long(argc, argv, short_options, long_options, 0);
        switch (next_option) {
            case 'd':
                return send_dbus_service_start_request();
                break;
            case 's':
                return send_dbus_service_stop_request();
                break;
            case 'b':
                return send_reboot_request();
                break;
            case 'v':
                return get_version();
                break;
            case 'a':
                return send_ta_test_request();
                break;
            case 't':
                return telinit(optarg);
                break;
            case 'l':
                {
                    unsigned long level;
                    errno = 0;
                    level = strtoul(optarg, 0, 10);
                    if (errno != 0 || level > 7) {
                        usage(program_name);
                        return EXIT_FAILURE;
                    }
                    return loglevel(atoi(optarg));
                }
            case 'h':
                usage(program_name);
                return EXIT_SUCCESS;
                break;
            case '?':
                usage(program_name);
                return EXIT_FAILURE;
                break;
        }
    } while (next_option != -1);

    /* check if unknown parameters or no parameters at all were given */
    if (argc == 1 || optind < argc) {
        usage(program_name);
        return EXIT_FAILURE;
    }

    return retval;
}
