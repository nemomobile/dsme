/**
   @file dsmetool.c

   Dsmetool can be used to send commands to DSME.
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

#include "dsme/protocol.h"
#include "../modules/lifeguard.h"
#include "../modules/dbusproxy.h"
#include "../modules/state.h"

#include <stdlib.h>
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

void usage(const char* name);
static int send_process_start_request(const char*       command,
                                      process_actions_t action,
                                      int               maxcount,
                                      int               maxperiod,
                                      uid_t             uid,
                                      gid_t             gid,
                                      int               nice);
static int send_process_stop_request(const char* command, int signal);
static int get_version(void);

static dsmesock_connection_t* conn;

void usage(const char* name)
{
    printf("USAGE: %s <options>\n", name);
    printf(
"Note that the <cmd> should include absolute path.\n"
"  -r --start-reset=<cmd>          Start a process\n"
"                                   (on process exit, do SW reset)\n"
"  -t --start-restart=<cmd>        Start a process\n"
"                                   (on process exit, restart max N times,\n"
"                                    then do SW reset)\n"
"  -f --start-restart-fail=<cmd>   Start a process\n"
"                                   (on process exit, restart max N times,\n"
"                                    then stop trying)\n"
"  -o --start-once=<cmd>           Start a process only once\n"
"  -c --max-count=N                Restart process only maximum N times\n"
"                                   in defined period of time\n"
"                                   (the default is 10 times in 60 s)\n"
"  -T --count-time=N               Set period for restart check\n"
"                                   (default 60 s)\n"
"  -k --stop=<cmd>                 Stop a process started with cmd\n"
"                                   (if started with dsme)\n"
"  -S --signal=N                   Set used signal for stopping processes\n"
"  -u --uid=N                      Set used uid for started process\n"
"  -U --user=<username>            Set used uid for started process\n"
"                                   from username\n"
"  -g --gid=N                      Set used gid for started process\n"
"  -G --group=<groupname>          Set used gid for started process\n"
"                                   from groupname\n"
"  -n --nice=N                     Set used nice value (priority)\n"
"                                   for started process\n"
"  -d --start-dbus                 Start DSME's D-Bus services\n"
#if 0 // TODO
"  -s --stop-dbus                  Stop DSME's D-Bus services\n"
#endif
"  -b --reboot                     Reboot the device\n"
"  -v --version                    Print the versions of DSME and dsmetool\n"
"  -h --help                       Print usage\n"
"\n"
"Examples:\n"
" dsmetool -U user -o \"/usr/bin/process --parameter\"\n"
" dsmetool -k \"/usr/bin/process --parameter\"\n");
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

static void send_to_dsme_with_extra(const void* msg,
                                    size_t      extra_size,
                                    const void* extra)
{
    if (dsmesock_send_with_extra(conn, msg, extra_size, extra) == -1) {
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

    while (1) {
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

static int send_process_start_request(const char*       command,
                                      process_actions_t action,
                                      int               maxcount,
                                      int               maxperiod,
                                      uid_t             uid,
                                      gid_t             gid,
                                      int               nice)
{
    DSM_MSGTYPE_PROCESS_START        msg =
      DSME_MSG_INIT(DSM_MSGTYPE_PROCESS_START);
    DSM_MSGTYPE_PROCESS_STARTSTATUS* retmsg;
    fd_set rfds;
    int    ret;

    msg.action         = action;
    msg.restart_limit  = maxcount;
    msg.restart_period = maxperiod;
    msg.uid            = uid;
    msg.gid            = gid;
    msg.nice           = nice;
    send_to_dsme_with_extra(&msg, strlen(command) + 1, command);

    while (1) {
        FD_ZERO(&rfds);
        FD_SET(conn->fd, &rfds);

        ret = select(conn->fd+1, &rfds, NULL, NULL, NULL);
        if (ret == -1) {
            printf("Error in select()\n");
            return -1;
        }

        retmsg = (DSM_MSGTYPE_PROCESS_STARTSTATUS*)dsmesock_receive(conn);

        if (DSMEMSG_CAST(DSM_MSGTYPE_PROCESS_STARTSTATUS, retmsg) == 0)
        {
            printf("Received invalid message (type: %i)\n",
                   dsmemsg_id((dsmemsg_generic_t*)retmsg));
            free(retmsg);
            continue;
        }

        /* printf("PID=%d, startval=%d\n", retmsg->pid, retmsg->return_value); */

        ret = retmsg->status;
        free(retmsg);

        return ret;
    }
}

static int send_process_stop_request(const char* command, int signal)
{
    DSM_MSGTYPE_PROCESS_STOP msg = DSME_MSG_INIT(DSM_MSGTYPE_PROCESS_STOP);

    msg.signal = signal;
    send_to_dsme_with_extra(&msg, strlen(command) + 1, command);

    return EXIT_SUCCESS;
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

int main(int argc, char* argv[])
{
    const char* program_name  = argv[0];
    int         next_option;
    int         retval        =  0;
    int         maxcount      = 10;
    int         countperiod   = 60;
    int         signum        = 15;
    uid_t       uid           = getuid();
    gid_t       gid           = getgid();
    int         group_set     = 0;
    const char* username      = 0;
    const char* group         = 0;
    int         nice          = 0;
    enum { NONE, START, STOP } action = NONE;
    const char* program       = "";
    process_actions_t policy  = ONCE;
    const char* short_options = "n:hr:f:t:o:c:T:k:S:u:g:U:G:dsbv";
    const struct option long_options[] = {
        {"help",               0, NULL, 'h'},
        {"start-reset",        1, NULL, 'r'},
        {"start-restart",      1, NULL, 't'},
        {"start-restart-fail", 1, NULL, 'f'},
        {"start-once",         1, NULL, 'o'},
        {"max-count",          1, NULL, 'c'},
        {"count-time",         1, NULL, 'T'},
        {"stop",               1, NULL, 'k'},
        {"signal",             1, NULL, 'S'},
        {"uid",                1, NULL, 'u'},
        {"gid",                1, NULL, 'g'},
        {"user",               1, NULL, 'U'},
        {"group",              1, NULL, 'G'},
        {"nice",               1, NULL, 'n'},
        {"start-dbus",         0, NULL, 'd'},
        {"stop-dbus",          0, NULL, 's'},
        {"reboot",             0, NULL, 'b'},
        {"version",            0, NULL, 'v'},
        {0, 0, 0, 0}
    };

    do {
        next_option =
            getopt_long(argc, argv, short_options, long_options, NULL);
        switch (next_option) {
            case 'k':
                program = optarg;
                action = STOP;
                break;
            case 'S':
                signum = atoi(optarg);
                break;
            case 'u':
                uid = atoi(optarg);
                break;
            case 'U':
                username = optarg;
                break;
            case 'g':
                gid = atoi(optarg);
                group_set = 1;
                break;
            case 'G':
                group = optarg;
                group_set = 1;
                break;
            case 'n':
                nice = atoi(optarg);
                break;
            case 'c':
                maxcount = atoi(optarg);
                break;
            case 'T':
                countperiod = atoi(optarg);
                break;
            case 'r':
                program = optarg;
                policy = RESET;
                action = START;
                break;
            case 't':
                program = optarg;
                policy = RESPAWN;
                action = START;
                break;
            case 'f':
                program = optarg;
                policy = RESPAWN_FAIL;
                action = START;
                break;
            case 'o':
                program = optarg;
                policy = ONCE;
                action = START;
                break;
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

    if (username != 0) {
        struct passwd *pw_entry = getpwnam(username);

        if (uid != getuid())
            printf("warning, username overrides specified uid\n");

        if (!pw_entry) {
            printf("Can't get a UID for username: %s\n", username); 
            return EXIT_FAILURE;
        }
        uid = pw_entry->pw_uid;
    }


    if (group != 0) {
        struct group* gr_entry = getgrnam(group);

        if (gid != getgid())
            printf("warning, group overrides specified gid\n");

        if (!gr_entry) {
            printf("Can't get a GID for groupname: %s\n", group);
            return EXIT_FAILURE;
        }
        gid = gr_entry->gr_gid;
    }

    if (uid != getuid() && !group_set) {
        struct passwd *pw_entry = getpwuid(uid);
        if (!pw_entry) {
            printf("Can't get pwentry for UID: %d\n", uid);
            return EXIT_FAILURE;
        }
        if (pw_entry->pw_gid)
            gid = pw_entry->pw_gid;
        else
            printf("Default group not found for UID: %d. Using current one.\n", uid);
    }

    connect_to_dsme();

    if (action == START) {
        retval = send_process_start_request(program,
                                            policy,
                                            maxcount,
                                            countperiod,
                                            uid,
                                            gid,
                                            nice);
    } else if (action == STOP) {
        send_process_stop_request(program, signum);
    }

    disconnect_from_dsme();

    return retval;
}
