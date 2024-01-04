#include <iot/mongoose.h>
#include <iot/iot.h>
#include "apmgr.h"
#include "callback.h"

static void start_ap_list_sync(struct mg_mgr *mgr) {
    struct apmgr_private *priv = (struct apmgr_private *)mgr->userdata;
    struct mg_mqtt_opts pub_opts;
    memset(&pub_opts, 0, sizeof(pub_opts));
    pub_opts.topic = mg_str("mg/apmgr/state/$iot-mqtt");
    pub_opts.message = mg_str("{\"method\": \"$mqtt/clients\"}");
    pub_opts.qos = MQTT_QOS, pub_opts.retain = false;
    mg_mqtt_pub(priv->mqtt_conn, &pub_opts);
    MG_INFO(("send %.*s -> %.*s", (int) pub_opts.message.len, pub_opts.message.ptr,
        (int) pub_opts.topic.len, pub_opts.topic.ptr));
}

static void do_ap_state_update(struct mg_mgr *mgr, struct ap *ap) {

    struct apmgr_private *priv = (struct apmgr_private *)mgr->userdata;
    if (ap->state < priv->cfg.opts->state_begin || ap->state >= priv->cfg.opts->state_end) {
        MG_ERROR(("invalid ap[%s] state: %d", ap->info.dev_id, ap->state));
        return;
    }

    if (ap->state_timeout > 0) { //wait for response
        if (mg_millis() > ap->state_timeout) {
            MG_INFO(("ap state update timeout: %s, state: %d -> %d", ap->info.dev_id, ap->state, ap->state - 1));
            ap->state = ap->state - 1; //enter prev state
            ap->state_timeout = 0;
        }
    } else {

        //gen request from callback lua
        struct mg_str request = gen_rpc_request(mgr, ap);
        if (!request.ptr) {
            MG_ERROR(("gen_rpc_request failed"));
            return;
        }

        MG_INFO(("ap state update %s, state: %d -> %d", ap->info.dev_id, ap->state, ap->state + 1));
        ap->state = ap->state + 1; //enter next state
        ap->state_timeout = mg_millis() + priv->cfg.opts->state_timeout*1000; //set timeout

        char *topic = mg_mprintf("device/%s/rpc/request/apmgr/%d", ap->info.dev_id, ap->state);
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

static void start_ap_state_update(struct mg_mgr *mgr) {
    struct apmgr_private *priv = (struct apmgr_private *)mgr->userdata;
    struct ap_event *e = priv->ap_events;
    while (e) {
        struct ap_event *tmp = e->next;
        if (e->ap->status.last_seen != priv->ap_list_synced_time || //offline
            e->ap->state == priv->cfg.opts->state_end) {//all synced, finish
            LIST_DELETE(struct ap_event, &priv->ap_events, e);
            MG_INFO(("ap[%s] state update finish", e->ap->info.dev_id));
            free(e);
        } else {
            do_ap_state_update(mgr, e->ap);
        }
        e = tmp;
    }
}


void timer_state_fn(void *arg) {
    struct mg_mgr *mgr = (struct mg_mgr *)arg;
    struct apmgr_private *priv = (struct apmgr_private *)mgr->userdata;

    if (!priv->mqtt_conn) return;

    if (!priv->ap_list_synced) {
        start_ap_list_sync(mgr);
    } else {
        start_ap_state_update(mgr);
    }

}