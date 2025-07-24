#include <iot/mongoose.h>
#include <iot/iot.h>
#include "controller.h"
#include "callback.h"

static void start_agent_list_sync(struct mg_mgr *mgr) {
    struct controller_private *priv = (struct controller_private *)mgr->userdata;
    struct mg_mqtt_opts pub_opts;
    memset(&pub_opts, 0, sizeof(pub_opts));
    pub_opts.topic = mg_str("mg/iot-controller/state/$iot-mqtt");
    pub_opts.message = mg_str("{\"method\": \"$mqtt/clients\"}");
    pub_opts.qos = MQTT_QOS, pub_opts.retain = false;
    mg_mqtt_pub(priv->mqtt_conn, &pub_opts);
    MG_INFO(("send %.*s -> %.*s", (int) pub_opts.message.len, pub_opts.message.ptr,
        (int) pub_opts.topic.len, pub_opts.topic.ptr));
}

static void do_agent_state_update(struct mg_mgr *mgr, struct agent *agent) {

    struct controller_private *priv = (struct controller_private *)mgr->userdata;
    if (agent->state < priv->cfg.opts->state_begin || agent->state >= priv->cfg.opts->state_end) {
        MG_ERROR(("invalid agent[%s] state: %d", agent->info.dev_id, agent->state));
        return;
    }

    if (agent->state_timeout > 0) { //wait for response
        if (mg_millis() > agent->state_timeout) {
            MG_INFO(("agent state update timeout: %s, state: %d -> %d", agent->info.dev_id, agent->state, agent->state - 1));
            agent->state = agent->state - 1; //enter prev state
            agent->state_timeout = 0;
            agent->state_stay = 0;
        }
    } else {

        if (agent->state_stay > 0) {
            agent->state_stay = agent->state_stay - 1;
            return;
        }

        //gen request from callback lua
        struct mg_str request = gen_rpc_request(mgr, agent);
        if (!request.ptr) {
            MG_ERROR(("gen_rpc_request failed"));
            return;
        }

        MG_INFO(("agent state update %s, state: %d -> %d", agent->info.dev_id, agent->state, agent->state + 1));
        agent->state = agent->state + 1; //enter next state
        agent->state_timeout = mg_millis() + priv->cfg.opts->state_timeout*1000; //set timeout

        char *topic = mg_mprintf("device/%s/rpc/request/iot-controller/%d", agent->info.dev_id, agent->state);
        struct mg_mqtt_opts pub_opts;
        memset(&pub_opts, 0, sizeof(pub_opts));
        pub_opts.topic = mg_str(topic);
        pub_opts.message = request;
        pub_opts.qos = MQTT_QOS, pub_opts.retain = false;
        mg_mqtt_pub(priv->mqtt_conn, &pub_opts);

        MG_INFO(("send %.*s -> %.*s", (int) pub_opts.message.len, pub_opts.message.ptr,
            (int) pub_opts.topic.len, pub_opts.topic.ptr));
        free(topic);
        free((void*)request.ptr);

    }
}

static void start_agent_state_update(struct mg_mgr *mgr) {

    struct controller_private *priv = (struct controller_private *)mgr->userdata;
    for (struct agent_list *agent_list = priv->agent_list; agent_list; agent_list = agent_list->next) {
        struct agent *agent = agent_list->agent;
        if (agent->status.last_seen != priv->agent_list_synced_time || //offline
            agent->state == priv->cfg.opts->state_end) {//all synced, finish
            continue;
        }
        do_agent_state_update(mgr, agent);
        usleep(1000);
    }
}


void timer_state_fn(void *arg) {
    struct mg_mgr *mgr = (struct mg_mgr *)arg;
    struct controller_private *priv = (struct controller_private *)mgr->userdata;

    if (priv->reset_all_agents) {
        priv->reset_all_agents = 0;
        // reset agents
        for (struct agent_list *agent_list = priv->agent_list; agent_list; agent_list = agent_list->next) {
            struct agent *agent = agent_list->agent;
            agent->state = priv->cfg.opts->state_begin;
            agent->state_timeout = 0;
            agent->state_stay = 0;
        }
        MG_INFO(("reset all agents state"));
    }

    if (!priv->mqtt_conn) return;

    if (!priv->agent_list_synced) {
        start_agent_list_sync(mgr);
    } else {
        start_agent_state_update(mgr);
    }

}