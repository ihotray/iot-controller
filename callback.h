#ifndef __CALLBACK_H__
#define __CALLBACK_H__

#include <iot/mongoose.h>
#include "ap.h"

//return request, must free by caller
struct mg_str gen_rpc_request(struct mg_mgr *mgr, struct ap *ap);

//return 0 if success, -1 if failed
int handle_rpc_response(struct mg_mgr *mgr, struct ap *ap, struct mg_str response);


#endif // __CALLBACK_H__