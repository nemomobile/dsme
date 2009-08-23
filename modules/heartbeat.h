#ifndef HEARTBEAT_H
#define HEARTBEAT_H

#include "dsme/messages.h"
#include <stdbool.h>

enum {
    DSME_MSG_ENUM(DSM_MSGTYPE_HEARTBEAT_START, 0x00000700),
    DSME_MSG_ENUM(DSM_MSGTYPE_HEARTBEAT_STOP,  0x00000701),
    DSME_MSG_ENUM(DSM_MSGTYPE_HEARTBEAT,       0x00000702),
};

typedef void (dsme_heartbeat_pre_cb_t)(void);
typedef bool (dsme_heartbeat_post_cb_t)(void);

typedef struct {
    DSMEMSG_PRIVATE_FIELDS
    dsme_heartbeat_pre_cb_t*  presleep_cb;
    unsigned                  sleep_interval_in_seconds;
    dsme_heartbeat_post_cb_t* postsleep_cb;
} DSM_MSGTYPE_HEARTBEAT_START;

typedef dsmemsg_generic_t DSM_MSGTYPE_HEARTBEAT_STOP;
typedef dsmemsg_generic_t DSM_MSGTYPE_HEARTBEAT;

#endif
