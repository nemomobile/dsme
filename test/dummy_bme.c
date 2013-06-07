/**
   @file dummy_bme.c

   Dummy bme server for ad hoc testing of dsme.
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

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/poll.h>
#include <sys/syslog.h>


#define BME_SRV_SOCK_PATH       "/tmp/.bmesrv"
#define BME_SRV_COOKIE          "BMentity"

#define BME_SERVER_BACKLOG 1		/* Max client connections */

#define FAILURE_RETRY_COUNT	3	/* We loop at most 3 times upon 'open' req */

#define EM_BATTERY_INFO_REQ                      0x06
#define EM_BATTERY_TEMP                          0x0004  /* -------------1-- */


#define bme_log(L, FMT ...) fprintf(stderr, FMT);

#include <sys/types.h>
typedef __uint8_t  uint8;
typedef __uint16_t uint16;
typedef __uint32_t uint32;

typedef struct {
    uint16      type, subtype;
} tBMEmsgGeneric;


struct emsg_battery_info_req {
    uint16      type, subtype;
    uint32      flags;
};

/* Battery info reply */
struct emsg_battery_info_reply {
    uint32      a;
    uint32      flags;
    uint16      c;
    uint16      d;
    uint16      temp;
    uint16      f;
    uint16      g;
    uint16      h;
    uint16      i;
    uint16      j;
    uint16      k;
    uint16      l;
};

union emsg_battery_info {
    struct emsg_battery_info_req   request;
    struct emsg_battery_info_reply reply;
};


union bme_ext_messages {
    union emsg_battery_info battery_info;

};


enum {
    UMSD,
    CCSD,
    NUM_POLL_FDS        /* Number of descriptors */
};

static volatile sig_atomic_t sig_caught = 0;


/* Client's socket descriptor that is in listening state */
int umsfd = -1;


/* Function prototypes */
void bme_server_shutdown(void);
int bme_server_init(void);
int bme_handle_new_client(int fd);
int em_srv_battery_info_req(struct emsg_battery_info_req *reqp, int client);
int bme_extmsg_handler(int client);
int bme_reply_client(int client, int status, void *msg, int size);
int bme_send_status_to_client(int client, int status);
void bme_shutdown(void);


/*
 * Initialize the server. Returns a negative value in case of failure.
 */
int bme_server_init(void)
{
    static const char *sockname = BME_SRV_SOCK_PATH;
    struct sockaddr_un addr;
    int size;

    /* Initialize the socket */
    if ((umsfd = socket(PF_UNIX, SOCK_STREAM, 0)) < 0) {
	bme_log(LOG_ERR, "socket: %s", strerror(errno));
	return -1;
    }

    unlink(sockname);

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = PF_UNIX;
    strncpy(addr.sun_path, sockname, sizeof(addr.sun_path) - 1);
    size = strlen(addr.sun_path) + sizeof(addr.sun_family);

    if (bind(umsfd, (struct sockaddr*) &addr, size) < 0) {
	bme_log(LOG_ERR, "bind: %s", strerror(errno));
	close(umsfd);
	umsfd = -1;
	return -1;
    }

    chmod(sockname, 0646);

    if (listen(umsfd, BME_SERVER_BACKLOG) < 0) {
	bme_log(LOG_ERR, "listen: %s", strerror(errno));
	close(umsfd);
	umsfd = -1;
	unlink(sockname);
	return -1;
    }

    return 0;
}


/*
 * Shut down the server
 */
void bme_server_shutdown(void)
{
    if (umsfd != -1) {
	close(umsfd);
	umsfd = -1;
    }
}


/*
 * Handle new client. Returns client's new fd if connection was successful,
 * a negative value otherwise.
 */
int bme_handle_new_client(int fd)
{
    struct sockaddr_un addr;
    socklen_t addr_len;
    int size, client;
    char cookie[8];

    addr_len = sizeof(addr);
    client = accept(fd, (struct sockaddr*) &addr, &addr_len);
    if (client < 0)
	return -1;

    /* Get the cookie, check it and acknowledge if cookie is correct */
    size = sizeof(cookie);
    if ((read(client, cookie, size) < size) ||
	(strncmp(cookie, BME_SRV_COOKIE, size)) ||
	(write(client, "\n", 1) < 1)) {
	close (client);
	return -1;
    }

    return client;
}

int em_srv_battery_info_req(struct emsg_battery_info_req *reqp, int client)
{
    struct emsg_battery_info_reply reply;
    uint32 flags;

    memset(&reply, 0, sizeof(reply));
    flags = reply.flags = reqp->flags;

    if (flags & EM_BATTERY_TEMP)
        reply.temp = 52;

    /* Send the reply */
    bme_reply_client(client, 0, &reply, sizeof(reply));

    return 1;
}

/*
 * Handle client's request.
 * Returns a negative value in case of an error, positive (or 0) otherwise.
 *
 * Currently we support only one client.
 */
int bme_extmsg_handler(int client)
{
    int res = -1, n;
    char buf[sizeof(union bme_ext_messages)];
    void *msgp = buf;
    tBMEmsgGeneric *gm = msgp;

    if ((n = read(client, buf, sizeof(buf))) < sizeof(*gm)) {
	bme_log(LOG_DEBUG, "got %d bytes instead of %zd from client's fd %d (possible disconnect)\n",
				n, sizeof(*gm), client);
	return -1;
    }
    bme_log(LOG_DEBUG, "message from client with fd=%d: type=%d, subtype=%d\n",
			client, gm->type, gm->subtype);

    switch (gm->type) {
      case EM_BATTERY_INFO_REQ:
        res = em_srv_battery_info_req(msgp, client);
	break;

      default:
        bme_log(LOG_NOTICE, "unknown message: type=%d, subtype=%d\n",
		gm->type, gm->subtype);
	bme_send_status_to_client(client, -1);
    }
    return res;
}


/*
 * Reply to the client.
 * Status (32-bit value) is sent first, then reply message.
 * Returns a negative value in case of error, positive value otherwise.
 */
int bme_reply_client(int client, int status, void *msg, int size)
{
    if (write(client, &status, sizeof(status)) != sizeof(status))
	return -1;
    if (write(client, msg, size) != size)
	return -1;
    return size;
}


/*
 * Send only a 32-bit status value to the client.
 */
int bme_send_status_to_client(int client, int status)
{
    return write(client, &status, sizeof(status));
}


void bme_shutdown()
{
    bme_server_shutdown();
    bme_log(LOG_INFO, "BME shutdown complete\n"); /* debug */
}

int main(int argc, char *argv[])
{
    struct pollfd ufds[NUM_POLL_FDS];
    int res, exit_status = EXIT_FAILURE;
    char *name;

    name = strrchr(argv[0], '/');
    name = name ? (name + 1) : argv[0];

    if (bme_server_init() < 0) {
        bme_log(LOG_ALERT, "cannot initialize server\n");
        goto exit;
    }

    /* Prepare for poll() */
    ufds[UMSD].fd = umsfd;		/* External messages */
    ufds[UMSD].events = POLLIN;
    ufds[CCSD].fd = -1;                 /* Connected client */
    ufds[CCSD].events = POLLIN;


    /*
     * Main loop. We poll socket descriptors, and call internal or external
     * message handler when data for it is available.
     */
    while (1) {
	if ((res = poll(ufds, NUM_POLL_FDS, -1)) > 0) {
	    if (ufds[UMSD].revents & POLLIN) {
		/* New client connection */
		if ((ufds[CCSD].fd = bme_handle_new_client(umsfd)) == -1) {
		    bme_log(LOG_INFO, "can't accept client\n");
		}
	    }
            if ((ufds[CCSD].revents & POLLIN) && (ufds[CCSD].fd != -1)) {
                /* External message */
                if (bme_extmsg_handler(ufds[CCSD].fd) < 0) {
                    /* The connection was closed */
                    close(ufds[CCSD].fd);
                    ufds[CCSD].fd = -1;
                }
            }
	} else {
	    /*
	     * poll() may be unblocked when asynchronous notification (signal)
	     * is received. That's a very normal situation.
	     */
	    if (errno != EINTR)
		bme_log(LOG_WARNING, "poll() returned %d, errno=%d\n", res, errno);
	}
    }

exit:
    res = atexit(bme_shutdown);
    if (res != 0) {
	    bme_log(LOG_NOTICE, "cannot set shutdown function, calling it explicitly\n");
	    bme_shutdown();
    }

    bme_log(LOG_INFO, "terminating with exit status %d\n", exit_status);

    return exit_status;
}
