#ifndef __AGENT_H__
#define __AGENT_H__

#define AGENT_ATTRIBUTE_LENGTH 64
#define AGENT_HASH_SIZE 63

struct agent_info {
    /* data stored in flash */
    char dev_id[AGENT_ATTRIBUTE_LENGTH]; //mac or sn, warnning: must be unique and don't contain '/'
};

struct agent_status {
    /* data stored in memory*/
    uint64_t connected;
    uint64_t last_seen;
    char ip[AGENT_ATTRIBUTE_LENGTH];
};

struct agent;
struct agent_list {
    struct agent_list *next;
    struct agent *agent;
};

struct agent {
    /* data */
    struct agent_list list;
    struct agent *next;

    struct agent_info info;
    struct agent_status status;

    //fsm
    //if state = state_n, then, state_n+1 is next state, state_n-1 is prev state.
    int state; // state of agent, connected -> sync info-> sync status-> sync config(if need)-> synced
    uint64_t state_timeout; //timeout of state
    uint64_t state_stay; //time of stay in state
};

#endif