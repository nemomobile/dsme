/**
   @file reboot.c

   Dsmetool can be used to send a reboot request to DSME.
   <p>
   Copyright (C) 2009-2010 Nokia Corporation.

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
#include <dsme/state.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

static int request_reboot(dsmesock_connection_t* conn)
{
    bool sent = false;

    DSM_MSGTYPE_REBOOT_REQ msg = DSME_MSG_INIT(DSM_MSGTYPE_REBOOT_REQ);
    if (dsmesock_send(conn, &msg) != -1) {
        sent = true;
    }

    return sent;
}

int main(int argc, char* argv[])
{
    dsmesock_connection_t* conn;

    if (!(conn = dsmesock_connect())) {
        perror("dsmesock_connect");
        return EXIT_FAILURE;
    }

    if (!request_reboot(conn)) {
        perror("dsmesock_send");
        return EXIT_FAILURE;
    }

    dsmesock_close(conn);

    return EXIT_SUCCESS;
}
