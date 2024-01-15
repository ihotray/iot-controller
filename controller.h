#ifndef __CONTROLLER_H__
#define __CONTROLLER_H__

#include <iot/mongoose.h>
#include "agent.h"

struct controller_option {
    const char *mqtt_serve_address;      //mqtt 服务地址
    const char *callback_lua;

    int mqtt_keepalive;                  //mqtt 保活间隔
    int debug_level;

    int state_begin; //agent state, start from
    int state_end;  //agent state, end to
    int state_timeout; //agent state, timeout
};

struct controller_config {
    struct controller_option *opts;
};

struct controller_private {
    struct controller_config cfg;

    struct mg_mgr mgr;

    struct mg_connection *mqtt_conn;
    uint64_t ping_active;
    uint64_t pong_active;

    bool agent_list_synced;  //has been sync agent list from mqtt server?
    uint64_t agent_list_synced_time; //sync agent list time

    int reset_all_agents; //reset agent state

    struct agent *agents[AGENT_HASH_SIZE];  //store agent in a hash list, hash by crc(agent->info.dev_id), always add or update, never delete

};

int controller_main(void *user_options);


#endif