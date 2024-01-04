#ifndef __APMGR_H__
#define __APMGR_H__

#include <iot/mongoose.h>
#include "ap.h"

struct apmgr_option {
    const char *mqtt_serve_address;      //mqtt 服务地址
    const char *callback_lua;

    int mqtt_keepalive;                  //mqtt 保活间隔
    int debug_level;

    int state_begin; //ap state, start from
    int state_end;  //ap state, end to
    int state_timeout; //ap state, timeout
};

struct apmgr_config {
    struct apmgr_option *opts;
};

struct apmgr_private {
    struct apmgr_config cfg;

    struct mg_mgr mgr;

    struct mg_connection *mqtt_conn;
    uint64_t ping_active;
    uint64_t pong_active;

    bool ap_list_synced;  //has been sync ap list from mqtt server?
    uint64_t ap_list_synced_time; //sync ap list time

    struct ap *aps[AP_HASH_SIZE];  //store ap in a hash list, hash by crc(ap->info.dev_id), always add or update, never delete
    struct ap_event *ap_events; //ap events list, handle by state machine, see state.c

};

int apmgr_main(void *user_options);


#endif