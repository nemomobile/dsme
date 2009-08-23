#include "heartbeat.h"
#include "dsme/modules.h"
#include "dsme/logging.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sched.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <glib.h>

#define DSME_STATIC_STRLEN(s) (sizeof(s) - 1)

#define DSME_HB_NICE      (-20)      /* least niceness */
#define DSME_HB_SCHEDULER SCHED_FIFO /* real-time scheduling */


static volatile bool             beating        = false;
static dsme_heartbeat_pre_cb_t*  presleep_cb    = 0;
static unsigned                  sleep_interval = 10;
static dsme_heartbeat_post_cb_t* postsleep_cb   = 0;
static int                       wake_up_pipe[2];


static void* heartbeat_thread(void* param)
{
    /* set process priority & scheduling */
    if (setpriority(PRIO_PROCESS, 0, DSME_HB_NICE) == -1) {
        fprintf(stderr, "DSME: setpriority(): %s\n", strerror(errno));
    }
    struct sched_param sch;
    memset(&sch, 0, sizeof(sch));
    sch.sched_priority = sched_get_priority_max(DSME_HB_SCHEDULER);
    if (sched_setscheduler(0, DSME_HB_SCHEDULER, &sch) == -1) {
        fprintf(stderr, "DSME: sched_setscheduler(): %s\n", strerror(errno));
    }

    /* lock to ram */
    if (mlockall(MCL_CURRENT|MCL_FUTURE) == -1) {
        fprintf(stderr, "DSME: mlockall(): %s\n", strerror(errno));
    }

    while (beating) {

        /* first sleep, doing callbacks just before and after the sleep */
        if (presleep_cb) (void)presleep_cb();

        struct timespec remaining_sleep_time = { sleep_interval, 0 };
        while (nanosleep(&remaining_sleep_time, &remaining_sleep_time) == -1 &&
               errno == EINTR)
        {
            /* EMPTY LOOP */
        }

        beating = postsleep_cb ? postsleep_cb() : true;

        /* then wake up the main thread to do less urgent tasks */
        ssize_t bytes_written;
        while ((bytes_written = write(wake_up_pipe[1], "*", 1)) == -1 &&
               (errno == EINTR))
        {
            /* EMPTY LOOP */
        }

        if (bytes_written != 1) {
            const char msg[] = "dsme: heartbeat: write() failure";
            write(STDERR_FILENO, msg, DSME_STATIC_STRLEN(msg));
            beating = false;
        }

    }

    const char msg[] = "dsme: heartbeat thread exiting";
    write(STDERR_FILENO, msg, DSME_STATIC_STRLEN(msg));

    return 0;
}

static gboolean emit_heartbeat_message(GIOChannel*  source,
                                       GIOCondition condition,
                                       gpointer     data)
{
    /* first read the byte that woke us up */
    ssize_t bytes_read;
    char    c;
    while ((bytes_read = read(wake_up_pipe[0], &c, 1)) == -1 &&
           errno == EINTR)
    {
        /* EMPTY LOOP */
    }

    if (bytes_read == 1) {
        /* got a wake up from heartbeat thread; send the heartbeat message */
        const DSM_MSGTYPE_HEARTBEAT beat = DSME_MSG_INIT(DSM_MSGTYPE_HEARTBEAT);
        broadcast_internally(&beat);
        dsme_log(LOG_DEBUG, "heartbeat");
        return true;
    } else {
        /* got an EOF (or a read error); remove the watch */
        dsme_log(LOG_DEBUG, "heartbeat: read() EOF or failure");
        return false;
    }
}

static bool start_heartbeat(void)
{
    /* create a non-blocking pipe for waking up the main thread */
    if (pipe(wake_up_pipe) == -1) {
        dsme_log(LOG_CRIT, "error creating wake up pipe: %s", strerror(errno));
        return false;
    }

    /* set writing end of the pipe to non-blocking mode */
    int flags;
    errno = 0;
    if ((flags = fcntl(wake_up_pipe[1], F_GETFL)) == -1 && errno != 0) {
        dsme_log(LOG_CRIT,
                 "error getting flags for wake up pipe: %s",
                 strerror(errno));
        goto close_and_fail;
    }
    if (fcntl(wake_up_pipe[1], F_SETFL, flags | O_NONBLOCK) == -1) {
        dsme_log(LOG_CRIT,
                 "error setting wake up pipe to non-blocking: %s",
                 strerror(errno));
        goto close_and_fail;
    }

    /* set up an I/O watch for the wake up pipe */
    GIOChannel* chan  = 0;
    guint       watch = 0;

    if (!(chan = g_io_channel_unix_new(wake_up_pipe[0]))) {
        goto close_and_fail;
    }
    if (!(watch = g_io_add_watch(chan, G_IO_IN, emit_heartbeat_message, 0))) {
        g_io_channel_unref(chan);
        goto close_and_fail;
    }
    g_io_channel_unref(chan);


    /* start the heartbeat thread */
    pthread_attr_t     tattr;
    pthread_t          tid;
    struct sched_param param;

    if (pthread_attr_init(&tattr) != 0) {
        dsme_log(LOG_CRIT, "Error getting thread attributes");
        goto remove_watch_close_and_fail;
    }
    if (pthread_attr_getschedparam(&tattr, &param) != 0) {
        dsme_log(LOG_CRIT, "Error getting scheduling parameters\n");
        goto remove_watch_close_and_fail;
    }
    beating = true;
    if (pthread_create(&tid, &tattr, heartbeat_thread, 0) != 0) {
        beating = false;
        dsme_log(LOG_CRIT, "Error creating the heartbeat thread\n");
        goto remove_watch_close_and_fail;
    }

    dsme_log(LOG_DEBUG, "started heartbeat at %u s interval", sleep_interval);

    return true;


remove_watch_close_and_fail:
    g_source_remove(watch);

close_and_fail:
    (void)close(wake_up_pipe[1]);
    (void)close(wake_up_pipe[0]);

    return false;
}

static void stop_heartbeat(void)
{
    beating = false;
    // TODO: join the heartbeat thread & close the pipe
}


DSME_HANDLER(DSM_MSGTYPE_HEARTBEAT_START, client, msg)
{
    if (!beating) {
        presleep_cb    = msg->presleep_cb;
        sleep_interval = msg->sleep_interval_in_seconds;
        postsleep_cb   = msg->postsleep_cb;

        start_heartbeat();
    }
}

DSME_HANDLER(DSM_MSGTYPE_HEARTBEAT_STOP, client, msg)
{
    stop_heartbeat();
}

module_fn_info_t message_handlers[] = {
    DSME_HANDLER_BINDING(DSM_MSGTYPE_HEARTBEAT_START),
    DSME_HANDLER_BINDING(DSM_MSGTYPE_HEARTBEAT_STOP),
    { 0 }
};


void module_init(module_t* handle)
{
    dsme_log(LOG_DEBUG, "libheartbeat.so loaded");
}

void module_fini(void)
{
    stop_heartbeat();

    dsme_log(LOG_DEBUG, "libheartbeat.so unloaded");
}
