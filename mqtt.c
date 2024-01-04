#include <iot/mongoose.h>
#include <iot/cJSON.h>
#include <iot/iot.h>
#include "apmgr.h"
#include "ap.h"
#include "callback.h"

static void mqtt_ev_open_cb(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {
    MG_INFO(("mqtt client connection created"));
}

static void mqtt_ev_error_cb(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {
    MG_ERROR(("%p %s", c->fd, (char *) ev_data));
    c->is_closing = 1;
}

static void mqtt_ev_poll_cb(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {

    struct apmgr_private *priv = (struct apmgr_private*)c->mgr->userdata;
    if (!priv->cfg.opts->mqtt_keepalive) //no keepalive
        return;

    uint64_t now = mg_millis();

    if (priv->pong_active && now > priv->pong_active &&
        now - priv->pong_active > (priv->cfg.opts->mqtt_keepalive + 3)*1000) { //TODO
        MG_INFO(("mqtt client connction timeout"));
        c->is_draining = 1;
    }

}

static void mqtt_ev_close_cb(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {

    struct apmgr_private *priv = (struct apmgr_private*)c->mgr->userdata;
    MG_INFO(("mqtt client connection closed"));
    priv->mqtt_conn = NULL; // Mark that we're closed

}

static void mqtt_ev_mqtt_open_cb(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {

    struct mg_str subt_mqtt_event = mg_str("$iot-mqtt-events");
    struct mg_str subt_mqtt_state = mg_str("mg/apmgr/state");
    struct mg_str subt_rpc_response = mg_str("device/+/rpc/response/apmgr/+");

    struct apmgr_private *priv = (struct apmgr_private*)c->mgr->userdata;

    MG_INFO(("connect to mqtt server: %s", priv->cfg.opts->mqtt_serve_address));
    struct mg_mqtt_opts sub_opts;
    memset(&sub_opts, 0, sizeof(sub_opts));
    sub_opts.topic = subt_mqtt_event;
    sub_opts.qos = MQTT_QOS;
    mg_mqtt_sub(c, &sub_opts);
    MG_INFO(("subscribed to %.*s", (int) subt_mqtt_event.len, subt_mqtt_event.ptr));

    sub_opts.topic = subt_mqtt_state;
    mg_mqtt_sub(c, &sub_opts);

    MG_INFO(("subscribed to %.*s", (int) subt_mqtt_state.len, subt_mqtt_state.ptr));

    sub_opts.topic = subt_rpc_response;
    mg_mqtt_sub(c, &sub_opts);

    MG_INFO(("subscribed to %.*s", (int) subt_rpc_response.len, subt_rpc_response.ptr));

}

static void mqtt_ev_mqtt_cmd_cb(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {

    struct mg_mqtt_message *mm = (struct mg_mqtt_message *) ev_data;
    struct apmgr_private *priv = (struct apmgr_private*)c->mgr->userdata;

    if (mm->cmd == MQTT_CMD_PINGRESP) {
        priv->pong_active = mg_millis();
    }
}

static void mqtt_ev_mqtt_msg_mqtt_events_cb(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {
    struct mg_mqtt_message *mm = (struct mg_mqtt_message *)ev_data;
    struct apmgr_private *priv = (struct apmgr_private*)c->mgr->userdata;
    if (!mg_strcmp(mm->data, mg_str("connected")) || !mg_strcmp(mm->data, mg_str("disconnected"))) {
        //mqtt client connected or disconnected, need sync client list
        //set ap_list_synced to false, timer_state_fn will send sync request message
        priv->ap_list_synced = false;
    }
}

static void mqtt_ev_mqtt_msg_mqtt_state_cb(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {
    struct mg_mqtt_message *mm = (struct mg_mqtt_message *)ev_data;
    struct apmgr_private *priv = (struct apmgr_private*)c->mgr->userdata;
    cJSON *root = cJSON_ParseWithLength(mm->data.ptr, mm->data.len);
    cJSON *code = cJSON_GetObjectItem(root, "code");
    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (cJSON_IsNumber(code) && (int)cJSON_GetNumberValue(code) == 0) {
        priv->ap_list_synced = true;
        priv->ap_list_synced_time = mg_millis()/1000;
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
                uint32_t index = crc % AP_HASH_SIZE;

                struct ap *ap = priv->aps[index];
                while (ap) {
                    if (!mg_casecmp(ap->info.dev_id, id_str)) {// maybe different dev_id, but same crc
                        break;
                    }
                    ap = ap->next;
                }
                if (ap == NULL) {
                    //add new ap
                    ap = calloc(1, sizeof(struct ap));
                    strncpy(ap->info.dev_id, id_str, sizeof(ap->info.dev_id)-1);
                    ap->status.connected = connected_t;
                    strncpy(ap->status.ip, ip_str, sizeof(ap->status.ip)-1);
                    ap->state = priv->cfg.opts->state_begin;
                    LIST_ADD_HEAD(struct ap, &priv->aps[index], ap);

                    //add ap event, handle by ap state machine
                    struct ap_event *e = calloc(1, sizeof(struct ap_event));
                    e->ap = ap;
                    LIST_ADD_HEAD(struct ap_event, &priv->ap_events, e);
                } else {
                    if (ap->status.connected < connected_t) { //handle reconnected same dev_id
                        //ap reconnected, update ap status
                        ap->status.connected = connected_t;
                        strncpy(ap->status.ip, ip_str, sizeof(ap->status.ip)-1);
                        ap->state = priv->cfg.opts->state_begin;
                        ap->state_timeout = 0;

                        //add ap event, handle by ap state machine
                        struct ap_event *e = calloc(1, sizeof(struct ap_event));
                        e->ap = ap;
                        LIST_ADD_HEAD(struct ap_event, &priv->ap_events, e);
                    }
                }
                ap->status.last_seen = priv->ap_list_synced_time;
            }
        }
    }
    cJSON_Delete(root);
#if 1
    if (priv->ap_list_synced) {
        for (size_t i = 0; i < sizeof(priv->aps)/sizeof(priv->aps[0]); i++) {
            struct ap *ap = priv->aps[i];
            while (ap) {
                MG_INFO(("[aplist][index: %d, synced at %llu] ap: %s, ip: %s, connected: %llu, last_seen: %llu, state: %d,  online: %d",
                i, priv->ap_list_synced_time, ap->info.dev_id, ap->status.ip,
                ap->status.connected, ap->status.last_seen, ap->state, priv->ap_list_synced_time == ap->status.last_seen));
                ap = ap->next;
            }
        }
    }
#endif
}

static void mqtt_ev_mqtt_msg_rpc_resp_cb(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {
    // get ap and state
    struct apmgr_private *priv = (struct apmgr_private*)c->mgr->userdata;
    struct mg_mqtt_message *mm = (struct mg_mqtt_message *)ev_data;
    char dev_id[AP_ATTRIBUTE_LENGTH] = {0};
    int state = 0;
    char *topic = mg_mprintf("%.*s", (int) mm->topic.len, mm->topic.ptr);
    // device/10:16:88:19:51:E8/rpc/response/apmgr/2
    int count = sscanf(topic, "device/%[^/rpc]/rpc/response/apmgr/%d", dev_id, &state);
    if (count != 2) {
        MG_ERROR(("invalid rpc response topic: %s, count: %d, dev_id: %s, state: %d", topic, count, dev_id, state));
        free(topic);
        return;
    }

    free(topic);

    //MG_INFO(("rpc response from %s, state: %d", dev_id, state));
    uint32_t crc = mg_crc32(0, dev_id, strlen(dev_id));
    struct ap *ap = priv->aps[crc % AP_HASH_SIZE];
    while (ap) {
        if (!mg_casecmp(ap->info.dev_id, dev_id)) {// maybe different dev_id, but same crc
            break;
        }
        ap = ap->next;
    }
    if (ap == NULL) {
        MG_ERROR(("ap not found: %s", dev_id));
        return;
    }
    if (ap->state != state) {
        MG_ERROR(("ap state not match: %s, state: %d, expect: %d", dev_id, state, ap->state));
        return;
    }

    if (handle_rpc_response(c->mgr, ap, mm->data)) {
        MG_ERROR(("handle rpc response failed"));
        return;
    }

    MG_INFO(("ap state update: %s, state: %d -> %d", ap->info.dev_id, ap->state, ap->state + 1));
    ap->state = ap->state + 1; //enter next state(finished state)
    ap->state_timeout = 0;
}

static void mqtt_ev_mqtt_msg_cb(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {

    struct mg_mqtt_message *mm = (struct mg_mqtt_message *)ev_data;
    MG_INFO(("received %.*s <- %.*s", (int) mm->data.len, mm->data.ptr,
        (int) mm->topic.len, mm->topic.ptr));

    if (!mg_strcmp(mm->topic, mg_str("$iot-mqtt-events"))) { //connect or disconnect event from mqtt server
        mqtt_ev_mqtt_msg_mqtt_events_cb(c, ev, ev_data, fn_data);
    } else if (!mg_strcmp(mm->topic, mg_str("mg/apmgr/state"))) { //ap list from mqtt server
        mqtt_ev_mqtt_msg_mqtt_state_cb(c, ev, ev_data, fn_data);
    } else if (mg_match(mm->topic, mg_str("device/*/rpc/response/apmgr/*"), NULL)) { //from iot-rpcd of ap
        mqtt_ev_mqtt_msg_rpc_resp_cb(c, ev, ev_data, fn_data);
    }
}

static void mqtt_cb(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {

    switch (ev) {
        case MG_EV_OPEN:
            mqtt_ev_open_cb(c, ev, ev_data, fn_data);
            break;

        case MG_EV_ERROR:
            mqtt_ev_error_cb(c, ev, ev_data, fn_data);
            break;

        case MG_EV_MQTT_OPEN:
            mqtt_ev_mqtt_open_cb(c, ev, ev_data, fn_data);
            break;

        case MG_EV_MQTT_CMD:
            mqtt_ev_mqtt_cmd_cb(c, ev, ev_data, fn_data);
            break;

        case MG_EV_MQTT_MSG:
            mqtt_ev_mqtt_msg_cb(c, ev, ev_data, fn_data);
            break;

        case MG_EV_POLL:
            mqtt_ev_poll_cb(c, ev, ev_data, fn_data);
            break;

        case MG_EV_CLOSE:
            mqtt_ev_close_cb(c, ev, ev_data, fn_data);
            break;
    }
}

// Timer function - recreate client connection if it is closed
void timer_mqtt_fn(void *arg) {
    struct mg_mgr *mgr = (struct mg_mgr *)arg;
    struct apmgr_private *priv = (struct apmgr_private*)mgr->userdata;
    uint64_t now = mg_millis();

    if (priv->mqtt_conn == NULL) {
        struct mg_mqtt_opts opts = {.clean = true,
                                .qos = MQTT_QOS,
                                .message = mg_str("goodbye"),
                                .keepalive = priv->cfg.opts->mqtt_keepalive};
        priv->mqtt_conn = mg_mqtt_connect(mgr, priv->cfg.opts->mqtt_serve_address, &opts, mqtt_cb, NULL);
        priv->ping_active = now;
        priv->pong_active = now;
        priv->ap_list_synced = false; //need sync ap list from mqtt server

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

}