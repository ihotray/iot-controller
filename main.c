#include <iot/mongoose.h>
#include <iot/iot.h>
#include "controller.h"


static void usage(const char *prog) {
    fprintf(stderr,
            "IoT-SDK v.%s\n"
            "Usage: %s OPTIONS\n"
            "  -s ADDR     - local mqtt server address, default: '%s'\n"
            "  -a n        - local mqtt timeout, default: '%d'\n"
            "  -x PATH     - iot-controller callback lua script, default: '%s'\n"
            "  -b n        - agent state begin, default: '%d', min: 1\n"
            "  -e n        - agent state end, default: '%d', min: begin+1\n"
            "  -t n        - agent state timeout, default: %d second\n"
            "  -v LEVEL    - debug level, from 0 to 4, default: %d\n"
            "\n"
            "  kill -USR1 `pidof %s` reset all agents state[to init state]\n"
            "\n",
            MG_VERSION, prog, MQTT_LISTEN_ADDR, 6, "/www/iot/handler/iot-controller.lua", 1, 5, 15, MG_LL_INFO, prog);

    exit(EXIT_FAILURE);
}

static void parse_args(int argc, char *argv[], struct controller_option *opts) {
    // Parse command-line flags
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0) {
            opts->mqtt_serve_address = argv[++i];
        } else if (strcmp(argv[i], "-a") == 0) {
            opts->mqtt_keepalive = atoi(argv[++i]);
            if (opts->mqtt_keepalive < 6) {
                opts->mqtt_keepalive = 6;
            }
        } else if (strcmp(argv[i], "-x") == 0) {
            opts->callback_lua = argv[++i];
        } else if (strcmp(argv[i], "-b") == 0) {
            opts->state_begin = atoi(argv[++i]);
            if (opts->state_begin < 1) {
                opts->state_begin = 1;
            }
        } else if (strcmp(argv[i], "-e") == 0) {
            opts->state_end = atoi(argv[++i]);
            if (opts->state_end < opts->state_begin) {
                opts->state_end = opts->state_begin+1;
            }
        } else if (strcmp(argv[i], "-t") == 0) {
            opts->state_timeout = atoi(argv[++i]);
            if (opts->state_timeout < 6) {
                opts->state_timeout = 6;
            }
        } else if (strcmp(argv[i], "-v") == 0) {
            opts->debug_level = atoi(argv[++i]);
        } else {
            usage(argv[0]);
        }
    }

    if (opts->state_begin >= opts->state_end) {
        usage(argv[0]);
    }
}

int main(int argc, char *argv[]) {

    struct controller_option opts = {
        .mqtt_serve_address = MQTT_LISTEN_ADDR,
        .mqtt_keepalive = 6,
        .callback_lua = "/www/iot/handler/iot-controller.lua",
        .debug_level = MG_LL_INFO,
        .state_begin = 1,
        .state_end = 5,
        .state_timeout = 15
    };

    parse_args(argc, argv, &opts);

    MG_INFO(("IoT-SDK version  : v%s", MG_VERSION));
    MG_INFO(("mqtt server      : %s", opts.mqtt_serve_address));
    MG_INFO(("mqtt keepalive   : %d", opts.mqtt_keepalive));
    MG_INFO(("callback lua     : %s", opts.callback_lua));
    MG_INFO(("agent state begin   : %d", opts.state_begin));
    MG_INFO(("agent state end     : %d", opts.state_end));
    MG_INFO(("agent state timeout : %d", opts.state_timeout));

    controller_main(&opts);
    return 0;
}