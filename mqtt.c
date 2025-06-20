#include <iot/mongoose.h>
#include <iot/cJSON.h>
#include <iot/iot.h>
#include "controller.h"
#include "agent.h"
#include "callback.h"

static void mqtt_ev_open_cb(struct mg_connection *c, int ev, void *ev_data) {
    MG_INFO(("mqtt client connection created"));
}

static void mqtt_ev_error_cb(struct mg_connection *c, int ev, void *ev_data) {
    MG_ERROR(("%p %s", c->fd, (char *) ev_data));
    c->is_closing = 1;
}

static void mqtt_ev_poll_cb(struct mg_connection *c, int ev, void *ev_data) {

    struct controller_private *priv = (struct controller_private*)c->mgr->userdata;
    if (!priv->cfg.opts->mqtt_keepalive) //no keepalive
        return;

    uint64_t now = mg_millis();

    if (priv->pong_active && now > priv->pong_active &&
        now - priv->pong_active > (priv->cfg.opts->mqtt_keepalive + 3)*1000) { //TODO
        MG_INFO(("mqtt client connction timeout"));
        c->is_draining = 1;
    }

}

static void mqtt_ev_close_cb(struct mg_connection *c, int ev, void *ev_data) {

    struct controller_private *priv = (struct controller_private*)c->mgr->userdata;
    MG_INFO(("mqtt client connection closed"));
    priv->mqtt_conn = NULL; // Mark that we're closed

}

static void mqtt_ev_mqtt_open_cb(struct mg_connection *c, int ev, void *ev_data) {

    struct mg_str subt_mqtt_event = mg_str("$iot-mqtt-events");
    struct mg_str subt_mqtt_state = mg_str("mg/iot-controller/state");
    struct mg_str subt_rpc_response = mg_str("device/+/rpc/response/iot-controller/+");

    struct controller_private *priv = (struct controller_private*)c->mgr->userdata;

    MG_INFO(("connect to mqtt server: %s", priv->cfg.opts->mqtt_serve_address));
    struct mg_mqtt_opts sub_opts;
    memset(&sub_opts, 0, sizeof(sub_opts));
    sub_opts.topic = subt_mqtt_event;
    sub_opts.qos = MQTT_QOS;
    mg_mqtt_sub(c, &sub_opts);
    MG_INFO(("subscribed to %.*s", (int) subt_mqtt_event.len, subt_mqtt_event.buf));

    sub_opts.topic = subt_mqtt_state;
    mg_mqtt_sub(c, &sub_opts);

    MG_INFO(("subscribed to %.*s", (int) subt_mqtt_state.len, subt_mqtt_state.buf));

    sub_opts.topic = subt_rpc_response;
    mg_mqtt_sub(c, &sub_opts);

    MG_INFO(("subscribed to %.*s", (int) subt_rpc_response.len, subt_rpc_response.buf));

}

static void mqtt_ev_mqtt_cmd_cb(struct mg_connection *c, int ev, void *ev_data) {

    struct mg_mqtt_message *mm = (struct mg_mqtt_message *) ev_data;
    struct controller_private *priv = (struct controller_private*)c->mgr->userdata;

    if (mm->cmd == MQTT_CMD_PINGRESP) {
        priv->pong_active = mg_millis();
    }
}

static void mqtt_ev_mqtt_msg_mqtt_events_cb(struct mg_connection *c, int ev, void *ev_data) {
    struct mg_mqtt_message *mm = (struct mg_mqtt_message *)ev_data;
    struct controller_private *priv = (struct controller_private*)c->mgr->userdata;
    if (!mg_strcmp(mm->data, mg_str("connected")) || !mg_strcmp(mm->data, mg_str("disconnected"))) {
        //mqtt client connected or disconnected, need sync client list
        //set agent_list_synced to false, timer_state_fn will send sync request message
        priv->agent_list_synced = false;
    }
}

static void mqtt_ev_mqtt_msg_mqtt_state_cb(struct mg_connection *c, int ev, void *ev_data) {
    struct mg_mqtt_message *mm = (struct mg_mqtt_message *)ev_data;
    struct controller_private *priv = (struct controller_private*)c->mgr->userdata;
    cJSON *root = cJSON_ParseWithLength(mm->data.buf, mm->data.len);
    cJSON *code = cJSON_GetObjectItem(root, "code");
    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (cJSON_IsNumber(code) && (int)cJSON_GetNumberValue(code) == 0) {
        priv->agent_list_synced_time = mg_millis()/1000;
        priv->agent_list_synced = true;
        if (cJSON_IsArray(data)) {
            int size = cJSON_GetArraySize(data);
            for (int i = 0; i < size; i++) {
                cJSON *item = cJSON_GetArrayItem(data, i);
                cJSON *id = cJSON_GetObjectItem(item, "u");
                cJSON *connected = cJSON_GetObjectItem(item, "c");
                cJSON *ip = cJSON_GetObjectItem(item, "a");

                if (!cJSON_IsString(id) || !cJSON_IsNumber(connected) || !cJSON_IsString(ip)) {
                    continue;
                }

                char *id_str = cJSON_GetStringValue(id);
                char *ip_str = cJSON_GetStringValue(ip);
                uint64_t connected_t = (uint64_t)cJSON_GetNumberValue(connected);

                uint32_t crc = mg_crc32(0, id_str, strlen(id_str));
                uint32_t index = crc % AGENT_HASH_SIZE;

                struct agent *agent = priv->agents[index];
                while (agent) {
                    if (!mg_casecmp(agent->info.dev_id, id_str)) {// maybe different dev_id, but same crc
                        break;
                    }
                    agent = agent->next;
                }
                if (agent == NULL) {
                    //add new agent
                    agent = calloc(1, sizeof(struct agent));
                    if (!agent) {
                        MG_ERROR(("calloc agent failed"));
                        continue;
                    }
                    agent->list.agent = agent;

                    strncpy(agent->info.dev_id, id_str, sizeof(agent->info.dev_id)-1);
                    agent->status.connected = connected_t;
                    strncpy(agent->status.ip, ip_str, sizeof(agent->status.ip)-1);
                    agent->state = priv->cfg.opts->state_begin;
                    LIST_ADD_HEAD(struct agent, &priv->agents[index], agent);
                    LIST_ADD_HEAD(struct agent_list, &priv->agent_list, &agent->list);

                } else {
                    if (agent->status.connected < connected_t) { //handle reconnected same dev_id
                        //agent reconnected, update agent status
                        agent->status.connected = connected_t;
                        strncpy(agent->status.ip, ip_str, sizeof(agent->status.ip)-1);
                        agent->state = priv->cfg.opts->state_begin;
                        agent->state_timeout = 0;
                        agent->state_stay = 0;

                    }
                }
                agent->status.last_seen = priv->agent_list_synced_time;
            }
        }
    }
    cJSON_Delete(root);
#if 1
    if (priv->agent_list_synced) {
        for (size_t i = 0; i < sizeof(priv->agents)/sizeof(priv->agents[0]); i++) {
            struct agent *agent = priv->agents[i];
            while (agent) {
                MG_INFO(("[agent list][index: %d, synced at %llu] agent: %s, ip: %s, connected: %llu, last_seen: %llu, state: %d,  online: %d",
                i, priv->agent_list_synced_time, agent->info.dev_id, agent->status.ip,
                agent->status.connected, agent->status.last_seen, agent->state, priv->agent_list_synced_time == agent->status.last_seen));
                agent = agent->next;
            }
        }
    }
#endif
}

static void mqtt_ev_mqtt_msg_rpc_resp_cb(struct mg_connection *c, int ev, void *ev_data) {
    // get agent and state
    struct controller_private *priv = (struct controller_private*)c->mgr->userdata;
    struct mg_mqtt_message *mm = (struct mg_mqtt_message *)ev_data;
    int state = -1;
    // device/1/rpc/response/iot-controller/2
    struct mg_str pattern = mg_str("device/*/rpc/response/iot-controller/*");

    struct mg_str caps[3] = { {0, 0}, {0, 0}, {0, 0} }; // device_id, state, rpc_id

    if (!mg_match(mm->topic, pattern, caps)) {
        MG_ERROR(("invalid rpc response topic: %.*s", mm->topic.len, mm->topic.buf));
        return;
    }

    struct mg_str str_dev_id = caps[0];
    struct mg_str str_state = caps[1];

    MG_INFO(("rpc response from %.*s, state: %.*s", str_dev_id.len, str_dev_id.buf, str_state.len, str_state.buf));
    uint32_t crc = mg_crc32(0, str_dev_id.buf, str_dev_id.len);
    struct agent *agent = priv->agents[crc % AGENT_HASH_SIZE];
    while (agent) {
        if (!mg_strcmp(mg_str(agent->info.dev_id), str_dev_id)) {// maybe different dev_id, but same crc
            break;
        }
        agent = agent->next;
    }
    if (agent == NULL) {
        MG_ERROR(("agent not found: %.*s", str_dev_id.len, str_dev_id.buf));
        return;
    }

    if (!mg_str_to_num(str_state, 10, &state, sizeof(state))) {
        MG_ERROR(("invalid state: %.*s", str_state.len, str_state.buf));
        return;
    };

    if (agent->state != state) {
        MG_ERROR(("agent state not match:  %.*s, state: %d, expect: %d", str_dev_id.len, str_dev_id.buf, state, agent->state));
        return;
    }

    struct handle_result result = handle_rpc_response(c->mgr, agent, mm->data);
    if ( result.code ) {
        MG_ERROR(("handle rpc response failed"));
        return;
    }

    int next_state = agent->state + 1;
    if (result.next_state > 0 ) {
        next_state = result.next_state;
    }

    int next_state_stay = 0;
    if (result.next_state_stay > 0) {
        next_state_stay = result.next_state_stay;
    }

    int next_state_timeout = 0;

    MG_INFO(("agent state update: %s, state: %d -> %d, next state stay: %d", agent->info.dev_id, agent->state, next_state, next_state_stay));
    agent->state = next_state; //enter next state
    agent->state_timeout = next_state_timeout;
    agent->state_stay = next_state_stay;
}

static void mqtt_ev_mqtt_msg_cb(struct mg_connection *c, int ev, void *ev_data) {

    struct mg_mqtt_message *mm = (struct mg_mqtt_message *)ev_data;
    MG_INFO(("received %.*s <- %.*s", (int) mm->data.len, mm->data.buf,
        (int) mm->topic.len, mm->topic.buf));

    if (!mg_strcmp(mm->topic, mg_str("$iot-mqtt-events"))) { //connect or disconnect event from mqtt server
        mqtt_ev_mqtt_msg_mqtt_events_cb(c, ev, ev_data);
    } else if (!mg_strcmp(mm->topic, mg_str("mg/iot-controller/state"))) { //iot-controller list from mqtt server
        mqtt_ev_mqtt_msg_mqtt_state_cb(c, ev, ev_data);
    } else if (mg_match(mm->topic, mg_str("device/*/rpc/response/iot-controller/*"), NULL)) { //from iot-rpcd of agent
        mqtt_ev_mqtt_msg_rpc_resp_cb(c, ev, ev_data);
    }
}

static void mqtt_cb(struct mg_connection *c, int ev, void *ev_data) {

    switch (ev) {
        case MG_EV_OPEN:
            mqtt_ev_open_cb(c, ev, ev_data);
            break;

        case MG_EV_ERROR:
            mqtt_ev_error_cb(c, ev, ev_data);
            break;

        case MG_EV_MQTT_OPEN:
            mqtt_ev_mqtt_open_cb(c, ev, ev_data);
            break;

        case MG_EV_MQTT_CMD:
            mqtt_ev_mqtt_cmd_cb(c, ev, ev_data);
            break;

        case MG_EV_MQTT_MSG:
            mqtt_ev_mqtt_msg_cb(c, ev, ev_data);
            break;

        case MG_EV_POLL:
            mqtt_ev_poll_cb(c, ev, ev_data);
            break;

        case MG_EV_CLOSE:
            mqtt_ev_close_cb(c, ev, ev_data);
            break;
    }
}

// Timer function - recreate client connection if it is closed
void timer_mqtt_fn(void *arg) {
    struct mg_mgr *mgr = (struct mg_mgr *)arg;
    struct controller_private *priv = (struct controller_private*)mgr->userdata;
    uint64_t now = mg_millis();

    if (priv->mqtt_conn == NULL) {
        struct mg_mqtt_opts opts = { 0 };

        opts.clean = true;
        opts.qos = MQTT_QOS;
        opts.message = mg_str("goodbye");
        opts.keepalive = priv->cfg.opts->mqtt_keepalive;

        priv->mqtt_conn = mg_mqtt_connect(mgr, priv->cfg.opts->mqtt_serve_address, &opts, mqtt_cb, NULL);
        priv->ping_active = now;
        priv->pong_active = now;
        priv->agent_list_synced = false; //need sync agent list from mqtt server

    } else if (priv->cfg.opts->mqtt_keepalive) { //need keep alive

        if (now < priv->ping_active) {
            MG_INFO(("system time loopback"));
            priv->ping_active = now;
            priv->pong_active = now;
        }
        if (now - priv->ping_active >= priv->cfg.opts->mqtt_keepalive * 1000) {
            mg_mqtt_ping(priv->mqtt_conn);
            priv->ping_active = now;
        }
    }

#if 1
    if (now/1000 - priv->agent_list_synced_time > 60 && priv->agent_list_synced) { //sync agent list every 60s
        priv->agent_list_synced = false;
    }
#endif

}