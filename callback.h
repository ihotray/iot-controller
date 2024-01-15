#ifndef __CALLBACK_H__
#define __CALLBACK_H__

#include <iot/mongoose.h>
#include "agent.h"

struct handle_result {
    int code;
    int next_state;
    int next_state_stay;
};

//return request, must free by caller
struct mg_str gen_rpc_request(struct mg_mgr *mgr, struct agent *agent);

//return 0 if success, -1 if failed
struct handle_result handle_rpc_response(struct mg_mgr *mgr, struct agent *agent, struct mg_str response);


#endif // __CALLBACK_H__