
#include <iot/mongoose.h>
#include "apmgr.h"
#include "mqtt.h"
#include "state.h"
#include "ap.h"


static int s_signo = 0;
static void signal_handler(int signo) {
    s_signo = signo;
}

int apmgr_init(void **priv, void *opts) {

    struct apmgr_private *p = NULL;
    int timer_opts = MG_TIMER_REPEAT | MG_TIMER_RUN_NOW;

    signal(SIGINT, signal_handler);   // Setup signal handlers - exist event
    signal(SIGTERM, signal_handler);  // manager loop on SIGINT and SIGTERM

    p = calloc(1, sizeof(struct apmgr_private));
    if (!p)
        return -1;

    p->cfg.opts = opts;
    mg_log_set(p->cfg.opts->debug_level);

    mg_mgr_init(&p->mgr);

    p->mgr.userdata = p;

    mg_timer_add(&p->mgr, 1000, timer_opts, timer_mqtt_fn, &p->mgr);
    mg_timer_add(&p->mgr, 3000, timer_opts, timer_state_fn, &p->mgr); 

    *priv = p;

    return 0;

}


void apmgr_run(void *handle) {
    struct apmgr_private *priv = (struct apmgr_private *)handle;
    while (s_signo == 0) mg_mgr_poll(&priv->mgr, 1000);  // Event loop, 1000ms timeout
}

void apmgr_exit(void *handle) {
    struct apmgr_private *priv = (struct apmgr_private *)handle;
    mg_mgr_free(&priv->mgr);

    for (size_t i = 0; i < sizeof(priv->aps)/sizeof(priv->aps[0]); i++) {//free aps
        struct ap *ap = priv->aps[i];
        while (ap) {
            struct ap *tmp = ap->next;
            LIST_DELETE(struct ap, &priv->aps[i], ap);
            free(ap);
            ap = tmp;
        }
    }

    struct ap_event *e = priv->ap_events;
    while (e) { //free events
        struct ap_event *tmp = e->next;
        LIST_DELETE(struct ap_event, &priv->ap_events, e);
        free(e);
        e = tmp;
    }
    free(handle);
}

int apmgr_main(void *user_options) {

    struct apmgr_option *opts = (struct apmgr_option *)user_options;
    void *apmgr_handle;
    int ret;

    ret = apmgr_init(&apmgr_handle, opts);
    if (ret)
        exit(EXIT_FAILURE);

    apmgr_run(apmgr_handle);

    apmgr_exit(apmgr_handle);

    return 0;

}