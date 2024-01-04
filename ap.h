#ifndef __AP_H__
#define __AP_H__

#define AP_ATTRIBUTE_LENGTH 64
#define AP_HASH_SIZE 63

struct ap_info {
    /* data stored in flash */
    char dev_id[AP_ATTRIBUTE_LENGTH]; //mac or sn, warnning: must be unique and don't contain '/'
};

struct ap_status {
    /* data stored in memory*/
    uint64_t connected;
    uint64_t last_seen;
    char ip[AP_ATTRIBUTE_LENGTH];
};

struct ap;
struct ap_event {
    struct ap_event *next;
    struct ap *ap;
};
struct ap {
    /* data */
    struct ap *next;
    struct ap_info info;
    struct ap_status status;

    //fsm
    //if state = state_n, then, state_n+1 is next state, state_n-1 is prev state.
    int state; // state of ap, connected -> sync info-> sync status-> sync config(if need)-> synced
    uint64_t state_timeout; //timeout of state
};



#endif