/**
   @brief IP heartbeat service dsme plug-in

   @file iphbd.c

   IP heartbeat service dsme plug-in

   <p>
   Copyright (C) 2010 Nokia. All rights reserved.

   @author Raimo Vuonnala <raimo.vuonnala@nokia.com>
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

#define _ISOC99_SOURCE
#define _GNU_SOURCE

#include <iphbd/libiphb.h>
#include <iphbd/iphb_internal.h>

#include "heartbeat.h"
#include "dsme/modules.h"
#include "dsme/modulebase.h"
#include "dsme/logging.h"
#include "dsme/timers.h"
#include "dsme/dsme-wdd-wd.h"

#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/time.h>

#include <glib.h>


#define DSME_MAX_EPOLL_EVENTS   10



/**@brief  Allocated structure of one client in the linked client list in iphbd */
typedef struct _client_t {
    int               fd;           /*!< IPC (Unix domain) socket or -1 */
    endpoint_t*       conn;         /*!< internal client endpoint (if fd == -1) */
    void*             data;         /*!< internal client cookie (if fd == -1) */
    time_t            wait_started; /*!< 0 if client has not subscribed to wake-up call */
    unsigned short    mintime;      /*!< min time to sleep in secs */
    unsigned short    maxtime;      /*!< max time to sleep in secs */
    pid_t             pid;          /*!< client process ID */
    struct _client_t* next;         /*!< pointer to the next client in the list (NULL if none) */
} client_t;

typedef bool (condition_func)(client_t* client, time_t now);


static bool epoll_add(int fd, void* ptr);

static gboolean read_epoll(GIOChannel*  source,
                           GIOCondition condition,
                           gpointer     data);
static int handle_wakeup_timeout(void* unused);
static bool handle_client_req(struct epoll_event* event, time_t now);
static bool handle_wait_req(const struct _iphb_wait_req_t* req,
                            client_t*                      client,
                            time_t                         now);
static condition_func mintime_passed;
static condition_func maxtime_passed;
static void wakeup_clients_if(condition_func should_wake_up, time_t now);
static int wakeup_clients_if2(condition_func should_wake_up, time_t now);
static bool wakeup(client_t* client, time_t now);
static void delete_clients(void);
static void delete_client(client_t* client);
static void remove_client(client_t* client, client_t* prev);
static void close_and_free_client(client_t* client);
static void sync_hwwd_feeder(void);
static void stop_wakeup_timer(void);


static client_t* clients = NULL;	/* linked lits of connected clients */

static int listenfd = -1; /* IPC client listen/accept handle */
static int kernelfd = -1; /* handle to the kernel */
static int epollfd  = -1; /* handle to the epoll instance */

static dsme_timer_t wakeup_timer = 0;


/**
   Open kernel module handle - retry later if fails (meaning LKM is not loaded)
*/
static void open_kernel_fd(void)
{
    static bool kernel_module_load_error_logged = false;

    kernelfd = open(HB_KERNEL_DEVICE, O_RDWR, 0644);
    if (kernelfd == -1) {
        kernelfd = open(HB_KERNEL_DEVICE_TEST, O_RDWR, 0644);
    }
    if (kernelfd == -1) {
        if (!kernel_module_load_error_logged) {
            kernel_module_load_error_logged = true;
            dsme_log(LOG_ERR,
                     "failed to open kernel connection '%s' (%s)",
                     HB_KERNEL_DEVICE,
                     strerror(errno));
        }
    } else {
        const char *msg;

        msg = HB_LKM_KICK_ME_PERIOD;

        dsme_log(LOG_DEBUG,
                 "opened kernel socket %d to %s, wakeup from kernel=%s",
                 kernelfd,
                 HB_KERNEL_DEVICE,
                 msg);

        if (write(kernelfd, msg, strlen(msg) + 1) == -1) {
            dsme_log(LOG_ERR,
                     "failed to write kernel message (%s)",
                     strerror(errno));
            // TODO: do something clever?
        } else if (!epoll_add(kernelfd, &kernelfd)) {
            dsme_log(LOG_ERR, "failed to add kernel fd to epoll set");
            // TODO: do something clever?
        }
    }
}

static void close_kernel_fd(void)
{
    if (kernelfd != -1) {
        if (epoll_ctl(epollfd, EPOLL_CTL_DEL, kernelfd, 0) == -1) {
            dsme_log(LOG_ERR, "failed to remove kernel fd from epoll set");
            // TODO: do something clever?
        }
        (void)close(kernelfd);
        dsme_log(LOG_DEBUG, "closed kernel socket %d", kernelfd);
        kernelfd = -1;
    }
}




/**
   Start up daemon. Does not fail if kernel module is not loaded
*/
// TODO: clean up in error cases (good god, C sucks here)
static bool start_service(void)
{
    struct sockaddr_un addr;

    listenfd = socket(PF_UNIX, SOCK_STREAM, 0);
    if (listenfd < 0) {
        dsme_log(LOG_ERR,
                 "failed to open client listen socket (%s)",
                 strerror(errno));
        goto fail;
    }
    unlink(HB_SOCKET_PATH);
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, HB_SOCKET_PATH);
    if (bind(listenfd, (struct sockaddr *) &addr, sizeof(addr))) {
        dsme_log(LOG_ERR,
                 "failed to bind client listen socket to %s, (%s)",
                 HB_SOCKET_PATH,
                 strerror(errno));
        goto close_and_fail;
    }
    if (chmod(HB_SOCKET_PATH, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH) !=
        0)
    {
        dsme_log(LOG_ERR,
                 "failed to chmod '%s' (%s)",
                 HB_SOCKET_PATH,
                 strerror(errno));
        goto close_and_fail;
    }
    if (listen(listenfd, 5) != 0) {
        dsme_log(LOG_ERR, "failed to listen client socket (%s)", strerror(errno));
        goto close_and_fail;
    }
    else {
        dsme_log(LOG_DEBUG,
                 "opened client socket %d to %s",
                 listenfd,
                 HB_SOCKET_PATH);
    }

    epollfd = epoll_create(10);
    if (epollfd == -1) {
        dsme_log(LOG_ERR, "failed to open epoll fd (%s)", strerror(errno));
        return false;
    }

    // add the listening socket to the epoll set
    if (!epoll_add(listenfd, &listenfd)) {
        goto close_and_fail;
    }

    // set up an I/O watch for the epoll set
    GIOChannel* chan  = 0;
    guint       watch = 0;
    if (!(chan = g_io_channel_unix_new(epollfd))) {
        goto close_and_fail;
    }
    watch = g_io_add_watch(chan, G_IO_IN, read_epoll, 0);
    g_io_channel_unref(chan);
    if (!watch) {
        goto close_and_fail;
    }

    return true;

close_and_fail:
    close(listenfd);
fail:
    return false;
}


/**
   Add new client to list.

   @param fd	Socket descriptor

   @todo	Is abort OK if malloc fails?

*/

static client_t* new_client(int fd)
{
    client_t* client;

    client = (client_t*)calloc(1, sizeof(client_t));
    if (client == 0) {
        errno = ENOMEM;
        dsme_log(LOG_ERR, "malloc(new_client) failed");
        abort(); // TODO
    }
    client->fd = fd;

    return client;
}

static client_t* new_internal_client(endpoint_t* conn, void* data)
{
    client_t* client = new_client(-1);
    client->conn = endpoint_copy(conn);
    client->data = data;

    return client;
}

static bool is_external_client(const client_t* client)
{
    return client->fd != -1;
}

static void list_add_client(client_t* newclient)
{
  client_t *client;

  if (NULL == clients) {
      /* first one */
      clients = newclient;
      return;
  } else {
      /* add to end */
      client = clients;
      while (client->next)
          client = client->next;
      client->next = newclient;
  }
}

static client_t* list_find_internal_client(endpoint_t* conn, void* data)
{
    client_t* client = clients;

    while (client) {
        if (!is_external_client(client)       &&
            endpoint_same(client->conn, conn) &&
            client->data == data)
        {
            break;
        }

        client = client->next;
    }

    return client;
}

static int external_clients()
{
    int       count  = 0;
    client_t* client = clients;

    while (client) {
        if (is_external_client(client)) {
            ++count;
        }
        client = client->next;
    }

    return count;
}




static void send_stats(client_t *client)
{
    struct iphb_stats stats   = { 0 };
    client_t*         c       = clients;
    unsigned int      next_hb = 0;
    time_t            now     = time(0);

    while (c) {
        stats.clients++;
        if (c->wait_started) {
            stats.waiting++;
        }

        if (c->wait_started) {
            unsigned int wait_time = c->wait_started + c->maxtime - now;
            if (!next_hb) {
                next_hb = wait_time;
            } else {
                if (wait_time < next_hb) {
                    next_hb = wait_time;
                }
            }
        }

        c = c->next;
    }

    stats.next_hb = next_hb;
    if (send(client->fd, &stats, sizeof(stats), MSG_DONTWAIT|MSG_NOSIGNAL) !=
        sizeof(stats))
    {
        dsme_log(LOG_ERR,
                 "failed to send to client with PID %lu (%s)",
                 (unsigned long)client->pid,
                 strerror(errno));  // do not drop yet
    }
}


static bool epoll_add(int fd, void* ptr)
{
  struct epoll_event ev = { 0, { 0 } };
  ev.events   = EPOLLIN;
  ev.data.ptr = ptr;
  if (epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
      dsme_log(LOG_ERR,
               "failed to add fd %d to epoll set (%s)",
               fd,
               strerror(errno));
      return false;
  }

  return true;
}

static gboolean read_epoll(GIOChannel*  source,
                           GIOCondition condition,
                           gpointer     data)
{
    dsme_log(LOG_DEBUG, "epollfd readable");

    stop_wakeup_timer();

    struct epoll_event events[DSME_MAX_EPOLL_EVENTS];
    int                nfds;
    int                i;
    condition_func*    wakeup_condition = maxtime_passed;

    while ((nfds = epoll_wait(epollfd, events, DSME_MAX_EPOLL_EVENTS, 0)) == -1
        && errno == EINTR)
    {
        /* EMPTY LOOP */
    }
    if (nfds == -1) {
        dsme_log(LOG_ERR, "epoll waiting failed (%s)", strerror(errno));
        // TODO: what to do? return false?
    }
    dsme_log(LOG_DEBUG, "epollfd_wait => %d events", nfds);

    time_t now = time(0);

    /* go through new events */
    for (i = 0; i < nfds; ++i) {
        if (events[i].data.ptr == &listenfd) {
            /* accept new clients */
            dsme_log(LOG_DEBUG, "accept() a new client");
            int newfd = accept(listenfd, 0, 0);
            if (newfd != -1) {
                client_t* client = new_client(newfd);
                if (epoll_add(newfd, client)) {
                    list_add_client(client);
                    dsme_log(LOG_DEBUG, "new client added to list");
                } else {
                    delete_client(client);
                }
            } else {
                dsme_log(LOG_ERR,
                         "failed to accept client (%s)",
                         strerror(errno));
            }
        } else if (events[i].data.ptr == &kernelfd) {
            wakeup_condition = mintime_passed;
            // tell the driver that we have dealt with the event
            while (read(kernelfd, 0, 0) == -1 && errno == EINTR);
        } else {
            /* deal with old clients */
            if (handle_client_req(&events[i], now)) {
                wakeup_condition = mintime_passed;
            }
        }
    }

    wakeup_clients_if(wakeup_condition, now);

    sync_hwwd_feeder();

    // TODO: should we ever stop?
    return true;
}

static int handle_wakeup_timeout(void* unused)
{
    dsme_log(LOG_DEBUG, "*** TIMEOUT ***");

    time_t now = time(0);

    wakeup_clients_if(maxtime_passed, now);

    sync_hwwd_feeder();

    return 0; /* stop the interval */
}

static bool is_timer_needed(int* optimal_sleep_time)
{
    bool   client_found = false;
    time_t now          = time(0);
    int    sleep_time   = 0;

    client_t* client = clients;
    while (client) {
        // only set up timers for external clients that are waiting
        if (is_external_client(client) && client->wait_started) {
            // does this client need to wake up before previous ones?
            if (!client_found ||
                client->wait_started + client->maxtime < now + sleep_time)
            {
                client_found = true;
                // make sure to keep sleep_time >= 0
                if (client->wait_started + client->maxtime <= now) {
                    // this client should have been woken up already!
                    sleep_time = 0;
                    break;
                } else {
                    // we have a new shortest sleep time
                    sleep_time = client->wait_started + client->maxtime - now;
                }
            }
        }

        client = client->next;
    }

    if (!client_found || sleep_time >= DSME_HEARTBEAT_INTERVAL) {
        // either no client or we will wake up before the timer anyway
        return false;
    } else {
        // a (short) timer has to be set up to guarantee a wakeup
        *optimal_sleep_time = sleep_time;
        return true;
    }
}


static bool handle_client_req(struct epoll_event* event, time_t now)
{
    client_t* client       = event->data.ptr;
    bool      client_woken = false;

    if (event->events & EPOLLERR ||
        event->events & EPOLLRDHUP ||
        event->events & EPOLLHUP)
    {
        dsme_log(LOG_DEBUG,
                 "client with PID %lu disappeared",
                 (unsigned long)client->pid);
        goto drop_client_and_fail;
    }

    dsme_log(LOG_DEBUG,
             "client with PID %lu is active",
             (unsigned long)client->pid);

    struct _iphb_req_t req = { 0 };

    if (recv(client->fd, &req, sizeof(req), MSG_WAITALL) <= 0) {
        dsme_log(LOG_ERR,
                 "failed to read from client with PID %lu (%s)",
                 (unsigned long)client->pid,
                 strerror(errno));
        goto drop_client_and_fail;
    }

    switch (req.cmd) {
        case IPHB_WAIT:
            client_woken = handle_wait_req(&req.u.wait, client, now);
            break;

        case IPHB_STAT:
            send_stats(client);
            break;

        default:
            dsme_log(LOG_ERR,
                     "client with PID %lu gave invalid command 0x%x, drop it",
                     (unsigned long)client->pid,
                     (unsigned int)req.cmd);
            goto drop_client_and_fail;
    }

    return client_woken;

drop_client_and_fail:
    delete_client(client);
    return false;
}

static bool handle_wait_req(const struct _iphb_wait_req_t* req,
                            client_t*                      client,
                            time_t                         now)
{
    bool client_woken = false;

    if (req->maxtime == 0 && req->mintime == 0) {
        if (!client->pid) {
            client->pid = req->pid;
            dsme_log(LOG_DEBUG,
                     "client with PID %lu connected",
                     (unsigned long)client->pid);
        } else {
            dsme_log(LOG_DEBUG,
                     "client with PID %lu canceled wait",
                     (unsigned long)client->pid);
            client_woken = true;
        }
        client->wait_started = 0;
        client->mintime      = req->mintime;
        client->maxtime      = req->maxtime;
    } else {
        if (req->mintime && req->maxtime == req->mintime) {
            dsme_log(client->pid ? LOG_WARNING : LOG_DEBUG,
                     "client with pid %lu signaled interest of waiting with"
                       " nonoptimal times (min=%d/max=%d)",
                     (unsigned long)client->pid,
                     (int)req->mintime,
                     (int)req->maxtime);
        } else {
            dsme_log(LOG_DEBUG,
                     "client with pid %lu signaled interest of waiting"
                       " (min=%d/max=%d)",
                     (unsigned long)client->pid,
                     (int)req->mintime,
                     (int)req->maxtime);
        }

        client->pid          = req->pid;
        client->wait_started = now;
        client->mintime      = req->mintime;
        client->maxtime      = req->maxtime;
    }

    return client_woken;
}


static void wakeup_clients_if(condition_func* should_wake_up, time_t now)
{
    // wake up clients in two passes,
    // giving priority to those whose maxtime has passed
    if (wakeup_clients_if2(maxtime_passed, now) ||
        should_wake_up == mintime_passed)
    {
        dsme_log(LOG_DEBUG, "waking up clients because somebody was woken up");
        wakeup_clients_if2(mintime_passed, now);
    }

    // open or close the kernel fd as needed
    if (!external_clients()) {
        close_kernel_fd();
    } else if (kernelfd == -1) {
        open_kernel_fd();
    }
}

static bool mintime_passed(client_t* client, time_t now)
{
    return now >= client->wait_started + client->mintime;
}

static bool maxtime_passed(client_t* client, time_t now)
{
    return now >= client->wait_started + client->maxtime;
}

static int wakeup_clients_if2(condition_func* should_wake_up, time_t now)
{
    int woken_up_clients = 0;

    client_t* prev   = 0;
    client_t* client = clients;
    while (client) {
        client_t* next = client->next;

        if (!client->wait_started) {
            dsme_log(LOG_DEBUG,
                     "client wid PID %lu is active, not to be woken up",
                     (unsigned long)client->pid);
        } else {
            if (should_wake_up(client, now)) {
                if (wakeup(client, now)) {
                    ++woken_up_clients;
                } else {
                    dsme_log(LOG_ERR,
                             "failed to send to client with PID %lu (%s),"
                               " drop client",
                             (unsigned long)client->pid,
                             strerror(errno));
                    remove_client(client, prev);
                    close_and_free_client(client);
                    goto next_client;
                }
            }
        }

        prev = client;
next_client:
        client = next;
    }

    return woken_up_clients;
}

static long long int timestamp(void)
{
    struct timeval tv;

    gettimeofday(&tv, 0);
    return tv.tv_sec * 1000000ll + tv.tv_usec;
}

static bool wakeup(client_t* client, time_t now)
{
    bool woken_up = false;

    if (is_external_client(client)) {
        struct _iphb_wait_resp_t resp = { 0 };
        resp.waited = now - client->wait_started;

        dsme_log(LOG_DEBUG,
                 "waking up client with PID %lu who has slept %lu secs"
                     ", ts=%lli",
                 (unsigned long)client->pid,
                 resp.waited,
                 timestamp());
        if (send(client->fd, &resp, sizeof(resp), MSG_DONTWAIT|MSG_NOSIGNAL) ==
            sizeof(resp))
        {
            woken_up = true;
        }
    } else {
        DSM_MSGTYPE_WAKEUP msg = DSME_MSG_INIT(DSM_MSGTYPE_WAKEUP);
        msg.resp.waited = now - client->wait_started;
        msg.data        = client->data;

        dsme_log(LOG_DEBUG,
                 "waking up internal client who has slept %lu secs",
                 msg.resp.waited);
        endpoint_send(client->conn, &msg);
        woken_up = true;
    }

    client->wait_started = 0;

    return woken_up;
}



static void delete_clients(void)
{
    while (clients) {
        delete_client(clients);
    }
}

static void delete_client(client_t* client)
{
    /* remove the client from the list */
    client_t* prev = 0;
    client_t* c    = clients;
    while (c) {
        if (client == c) {
            remove_client(client, prev);
            break;
        }

        prev = c;
        c = c->next;
    }

    close_and_free_client(client);
}

static void remove_client(client_t* client, client_t* prev)
{
    if (prev) {
        prev->next = client->next;
    }
    if (client == clients) {
        clients = client->next;
    }
}

static void close_and_free_client(client_t* client)
{
    if (is_external_client(client)) {
        (void)epoll_ctl(epollfd, EPOLL_CTL_DEL, client->fd, 0);
        (void)close(client->fd);
    } else {
        endpoint_free(client->conn);
    }

    free(client);
}


// synchronice to HW WD feeding process by listening to its heartbeat
DSME_HANDLER(DSM_MSGTYPE_HEARTBEAT, conn, msg)
{
    dsme_log(LOG_DEBUG, "HEARTBEAT from HWWD");

    stop_wakeup_timer();

    time_t now = time(0);

    // TODO: should we wake up mintime sleepers to sync on hwwd?
    wakeup_clients_if(maxtime_passed, now);
}

static void sync_hwwd_feeder(void)
{
    kill(getppid(), SIGHUP);
}

DSME_HANDLER(DSM_MSGTYPE_WAIT, conn, msg)
{
    dsme_log(LOG_DEBUG, "WAIT req from an internal client");

    stop_wakeup_timer();

    time_t now = time(0);

    client_t* client = list_find_internal_client(conn, msg->data);
    if (!client) {
        client = new_internal_client(conn, msg->data);
        list_add_client(client);
    }

    handle_wait_req(&msg->req, client, now);

    // we don't want to wake anyone else up for internal clients showing up
    // TODO: or do we?

    sync_hwwd_feeder();
}

DSME_HANDLER(DSM_MSGTYPE_IDLE, conn, msg)
{
    // the internal msg queue is empty so we will probably sleep for a while;
    // see if we need to set up a timer to guarantee a timely wakeup
    if (!wakeup_timer) {
        int sleep_time;
        if (is_timer_needed(&sleep_time)) {
            dsme_log(LOG_DEBUG, "setting a wakeup in %d s", sleep_time);
            wakeup_timer =
                dsme_create_timer_high_priority(sleep_time,
                                                handle_wakeup_timeout,
                                                0);
            dsme_log(LOG_DEBUG, "%s", "");
        }
    }
}

static void stop_wakeup_timer(void)
{
    if (wakeup_timer) {
        dsme_destroy_timer(wakeup_timer);
        wakeup_timer = 0;
    }
}


module_fn_info_t message_handlers[] = {
    DSME_HANDLER_BINDING(DSM_MSGTYPE_HEARTBEAT),
    DSME_HANDLER_BINDING(DSM_MSGTYPE_WAIT),
    DSME_HANDLER_BINDING(DSM_MSGTYPE_IDLE),
    { 0 }
};


void module_init(module_t* handle)
{
    dsme_log(LOG_DEBUG, "iphb.so loaded");

    if (!start_service()) {
        dsme_log(LOG_ERR, "iphb not started");
    }
}

void module_fini(void)
{
    delete_clients();

    if (listenfd != -1) {
        close(listenfd);
    }

    if (kernelfd != -1) {
        close(kernelfd);
    }

    dsme_log(LOG_DEBUG, "iphb.so unloaded");
}
