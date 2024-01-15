
#include <iot/mongoose.h>
#include "controller.h"
#include "mqtt.h"
#include "state.h"
#include "agent.h"


static int s_signo = 0;
static int s_sig_reset_all_agents = 0;
static void signal_handler(int signo) {
    switch (signo) {
    case SIGUSR1:
        s_sig_reset_all_agents = 1;
        break;
    default:
        s_signo = signo;
        break;
    }
}

int controller_init(void **priv, void *opts) {

    struct controller_private *p = NULL;
    int timer_opts = MG_TIMER_REPEAT | MG_TIMER_RUN_NOW;

    signal(SIGINT, signal_handler);   // Setup signal handlers - exist event
    signal(SIGTERM, signal_handler);  // manager loop on SIGINT and SIGTERM
    signal(SIGUSR1, signal_handler);  // SIGUSR1 for reset all agent's state

    p = calloc(1, sizeof(struct controller_private));
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

void controller_run(void *handle) {
    struct controller_private *priv = (struct controller_private *)handle;
    while (s_signo == 0) {
        if (s_sig_reset_all_agents) {
            s_sig_reset_all_agents = 0;
            priv->reset_all_agents = 1;
        }
        mg_mgr_poll(&priv->mgr, 1000);  // Event loop, 1000ms timeout
    }
}

void controller_exit(void *handle) {
    struct controller_private *priv = (struct controller_private *)handle;
    mg_mgr_free(&priv->mgr);

    for (size_t i = 0; i < sizeof(priv->agents)/sizeof(priv->agents[0]); i++) {//free agents
        struct agent *agent = priv->agents[i];
        while (agent) {
            struct agent *tmp = agent->next;
            LIST_DELETE(struct agent, &priv->agents[i], agent);
            free(agent);
            agent = tmp;
        }
    }

    struct agent_event *e = priv->agent_events;
    while (e) { //free events
        struct agent_event *tmp = e->next;
        LIST_DELETE(struct agent_event, &priv->agent_events, e);
        free(e);
        e = tmp;
    }
    free(handle);
}

int controller_main(void *user_options) {

    struct controller_option *opts = (struct controller_option *)user_options;
    void *controller_handle;
    int ret;

    ret = controller_init(&controller_handle, opts);
    if (ret)
        exit(EXIT_FAILURE);

    controller_run(controller_handle);

    controller_exit(controller_handle);

    return 0;

}