/**
   @file dsmetool.c

   Dsmetool can be used to send commands to DSME.
   <p>
   Copyright (C) 2004-2011 Nokia Corporation.

   @author Ismo Laitinen <ismo.laitinen@nokia.com>
   @author Semi Malinen <semi.malinen@nokia.com>
   @author Matias Muhonen <ext-matias.muhonen@nokia.com>
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
#include <poll.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <linux/rtc.h>

#include <pwd.h>
#include <grp.h>

#define STRINGIFY(x)  STRINGIFY2(x)
#define STRINGIFY2(x) #x


static dsmesock_connection_t* conn;

static int get_version(bool testmode);

static void usage(const char* name)
{
    printf("USAGE: %s <options>\n", name);
    printf(
"  -d --start-dbus                 Start DSME's D-Bus services\n"
"  -b --reboot                     Reboot the device\n"
"  -o --shutdown                   Shutdown (or switch to ACTDEAD)\n"
"  -u --powerup                    Switch from ACTDEAD to USER state\n"
"  -v --version                    Print the versions of DSME and dsmetool\n"
"  -t --telinit <runlevel name>    Change runlevel\n"
"  -l --loglevel <0..7>            Change DSME's logging verbosity\n"
"  -c --clear-rtc                  Clear RTC alarms\n"
"  -g --get-state                  Print device state, i.e. one of\n"
"                                   SHUTDOWN USER ACTDEAD REBOOT BOOT NOT_SET\n"
"                                   TEST MALF LOCAL or UNKNOWN\n"
"  -h --help                       Print usage\n");
}

static void connect_to_dsme(void)
{
    if( conn != 0 ) {
        /* If we ever get here, there is something
         * wrong with logic somewhere ... */
        fprintf(stderr, "Double connect detected\n");
        exit(EXIT_FAILURE);
    }
    conn = dsmesock_connect();
    if (conn == 0) {
        perror("dsmesock_connect");
        exit(EXIT_FAILURE);
    }
    if (conn->fd < 0) {
        perror("dsmesock_connect");
        exit(EXIT_FAILURE);
    }
    /* This gives enough time for DSME to check
       the socket permissions before we close the socket
       connection */
    (void)get_version(true);
}

static void disconnect_from_dsme(void)
{
    dsmesock_close(conn), conn = 0;
}

static void send_to_dsme(const void* msg)
{
    if (dsmesock_send(conn, msg) == -1) {
        perror("dsmesock_send");
        exit(EXIT_FAILURE);
    }
}

static bool wait_message_from_dsme(void)
{
    struct pollfd pfd =
    {
	.fd = conn->fd,
	.events = POLLIN,
    };

    return poll(&pfd, 1, 5000) == 1;
}

static dsmemsg_generic_t *read_message_from_dsme(void)
{
    dsmemsg_generic_t *msg = dsmesock_receive(conn);
    if( !msg ) {
        perror("dsmesock_receive");
        exit(EXIT_FAILURE);
    }
    return msg;
}

static void send_to_dsme_with_string(const void* msg, const char* s)
{
    if (dsmesock_send_with_extra(conn, msg, strlen(s) + 1, s) == -1) {
        perror("dsmesock_send_with_extra");
        exit(EXIT_FAILURE);
    }
}

static int get_version(bool testmode)
{
    DSM_MSGTYPE_GET_VERSION   req_msg =
          DSME_MSG_INIT(DSM_MSGTYPE_GET_VERSION);
    void*                     p = NULL;
    DSM_MSGTYPE_DSME_VERSION* retmsg = NULL;
    fd_set                    rfds;
    int                       ret = -1;
    int                       err = -1; /* assume failure */

    if (!testmode) {
        printf("dsmetool version: %s\n", STRINGIFY(PRG_VERSION));
        connect_to_dsme();
    }

    send_to_dsme(&req_msg);

    while (conn != 0 && conn->fd >= 0) {
        FD_ZERO(&rfds);
        FD_SET(conn->fd, &rfds);
        struct timeval tv;

        tv.tv_sec = 5;
        tv.tv_usec = 0;

        ret = select(conn->fd+1, &rfds, NULL, NULL, &tv);
        if (ret == -1) {
            fprintf(stderr, "Error in select()\n");
            break;
        }
        if (ret == 0) {
            fprintf(stderr, "Timeout when getting the DSME version\n");
            break;
        }

        p = dsmesock_receive(conn);
        if (p == 0) {
            fprintf(stderr, "Received NULL message\n");
            break;
        }
        if ((retmsg = DSMEMSG_CAST(DSM_MSGTYPE_DSME_VERSION, p)) == 0) {
            fprintf(stderr, "Received invalid message\n");
            free(p), p = 0;
            continue;
        }
        /* we got a valid reply message */
        err = 0;
        break;
    }

    if (!testmode && retmsg) {
        char* version = (char*)DSMEMSG_EXTRA(retmsg);
        if (version != 0) {
            version[DSMEMSG_EXTRA_SIZE(retmsg) - 1] = '\0';
            printf("DSME version: %s\n", version);
        }
    }

    free(p);
    if (!testmode) {
        disconnect_from_dsme();
    }

    return err;
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

static int send_shutdown_request()
{
    DSM_MSGTYPE_SHUTDOWN_REQ msg = DSME_MSG_INIT(DSM_MSGTYPE_SHUTDOWN_REQ);

    connect_to_dsme();
    send_to_dsme(&msg);
    disconnect_from_dsme();

    return EXIT_SUCCESS;
}

static int send_powerup_request()
{
    DSM_MSGTYPE_POWERUP_REQ msg = DSME_MSG_INIT(DSM_MSGTYPE_POWERUP_REQ);

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

static const char *dsme_state_repr(dsme_state_t state)
{
    const char *repr = "UNKNOWN";

    switch( state ) {
    case DSME_STATE_SHUTDOWN:	repr = "SHUTDOWN"; break;
    case DSME_STATE_USER:	repr = "USER";     break;
    case DSME_STATE_ACTDEAD:	repr = "ACTDEAD";  break;
    case DSME_STATE_REBOOT:	repr = "REBOOT";   break;
    case DSME_STATE_BOOT:	repr = "BOOT";     break;
    case DSME_STATE_NOT_SET:	repr = "NOT_SET";  break;
    case DSME_STATE_TEST:	repr = "TEST";     break;
    case DSME_STATE_MALF:	repr = "MALF";     break;
    case DSME_STATE_LOCAL:	repr = "LOCAL";    break;
    default: break;
    }

    return repr;
}

static int dsme_state_query()
{
    DSM_MSGTYPE_STATE_QUERY req = DSME_MSG_INIT(DSM_MSGTYPE_STATE_QUERY);
    dsme_state_t state = DSME_STATE_NOT_SET;

    connect_to_dsme();
    send_to_dsme(&req);

    while( wait_message_from_dsme() ) {
	dsmemsg_generic_t *msg = read_message_from_dsme();
	DSM_MSGTYPE_STATE_CHANGE_IND *rsp =
	    DSMEMSG_CAST(DSM_MSGTYPE_STATE_CHANGE_IND, msg);

	if( rsp )
	    state = rsp->state;

	free(msg);

	if( rsp )
	    break;
    }

    printf("%s\n", dsme_state_repr(state));

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

static int clear_rtc()
{
  /* Clear possible RTC alarm wakeup*/

    static const char rtc_path[] = "/dev/rtc0";
    int rtc_fd = -1;
    struct rtc_wkalrm alrm;

    if ((rtc_fd = open(rtc_path, O_RDONLY)) == -1) {
        /* TODO:
         * If open fails reason is most likely that dsme is running and has opened rtc.
         * In that case we should send message to dsme and ask it to do the clearing.
         * This functionality is not now needed because rtc alarms are cleared
         * only during preinit and there dsme is not running.
         * But to make this complete, that functionality should be added.
         */
        printf("Failed to open %s: %m\n", rtc_path);
        return EXIT_FAILURE;
    }

    memset(&alrm, 0, sizeof(alrm));
    if (ioctl(rtc_fd, RTC_WKALM_RD, &alrm) == -1) {
        printf("Failed to read rtc alarms %s: %s: %m\n", rtc_path, "RTC_WKALM_RD");
        close(rtc_fd);
        return EXIT_FAILURE;
    }
    printf("Alarm was %s at %d.%d.%d %02d:%02d:%02d UTC\n",
           alrm.enabled ? "Enabled" : "Disabled",
           1900+alrm.time.tm_year, 1+alrm.time.tm_mon, alrm.time.tm_mday, 
           alrm.time.tm_hour, alrm.time.tm_min, alrm.time.tm_sec);

    /* Because of bug? we need to enable alarm first before we can disable it */
    alrm.enabled = 1;
    alrm.pending = 0;
    if (ioctl(rtc_fd, RTC_WKALM_SET, &alrm) == -1)
        printf("Failed to enable rtc alarms %s: %s: %m\n", rtc_path, "RTC_WKALM_SET");
    /* Now disable the alarm */
    alrm.enabled = 0;
    alrm.pending = 0;
    if (ioctl(rtc_fd, RTC_WKALM_SET, &alrm) == -1) {
        printf("Failed to clear rtc alarms %s: %s: %m\n", rtc_path, "RTC_WKALM_SET");
        close(rtc_fd);
        return EXIT_FAILURE;
    }
    close(rtc_fd);
    printf("RTC alarm cleared ok\n");
    return EXIT_SUCCESS;
}

static unsigned parse_unsigned(char *str)
{
    char     *pos = str;
    unsigned  val = strtoul(str, &pos, 0);

    if( pos == str || *pos != 0 ) {
	fprintf(stderr, "%s: not a valid unsigned integer\n", str);
	exit(EXIT_FAILURE);
    }

    return val;
}

static unsigned parse_loglevel(char *str)
{
    unsigned val = parse_unsigned(str);

    if( val > 7 ) {
	fprintf(stderr, "%s: not a valid log level\n", str);
	exit(EXIT_FAILURE);
    }

    return val;
}

int main(int argc, char* argv[])
{
    const char* program_name  = argv[0];
    int         retval        = EXIT_SUCCESS;
    const char* short_options = "hdsbvact:l:guo";
    const struct option long_options[] = {
        {"help",       no_argument,       NULL, 'h'},
        {"start-dbus", no_argument,       NULL, 'd'},
        {"stop-dbus",  no_argument,       NULL, 's'},
        {"reboot",     no_argument,       NULL, 'b'},
        {"version",    no_argument,       NULL, 'v'},
        {"ta-test",    no_argument,       NULL, 'a'},
        {"clear-rtc",  no_argument,       NULL, 'c'},
        {"get-state",  no_argument,       NULL, 'g'},
        {"powerup",    no_argument,       NULL, 'u'},
        {"shutdown",   no_argument,       NULL, 'o'},
        {"telinit",    required_argument, NULL, 't'},
        {"loglevel",   required_argument, NULL, 'l'},
        {0, 0, 0, 0}
    };

    for( ;; ) {
	int opt = getopt_long(argc, argv, short_options, long_options, 0);

	if( opt == -1 )
	    break;

        switch( opt ) {
	case 'd':
	    return send_dbus_service_start_request();

	case 's':
	    return send_dbus_service_stop_request();

	case 'b':
	    return send_reboot_request();

	case 'u':
	    return send_powerup_request();

	case 'o':
	    return send_shutdown_request();

	case 'v':
	    return get_version(false);

	case 'a':
	    return send_ta_test_request();

	case 't':
	    return telinit(optarg);

	case 'g':
	    return dsme_state_query();

	case 'l':
	    return loglevel(parse_loglevel(optarg));

	case 'c':
	    return clear_rtc();

	case 'h':
	    usage(program_name);
	    return EXIT_SUCCESS;

	case '?':
	    fprintf(stderr, "(use --help for instructions)\n");
	    return EXIT_FAILURE;
        }
    }

    /* check if unknown parameters or no parameters at all were given */
    if (argc == 1 || optind < argc) {
        usage(program_name);
        return EXIT_FAILURE;
    }

    return retval;
}
