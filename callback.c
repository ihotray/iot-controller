#include <lualib.h>
#include <lauxlib.h>
#include <iot/mongoose.h>
#include <iot/cJSON.h>
#include "controller.h"
#include "agent.h"
#include "callback.h"

// call callback lua function gen_rpc_request, return request, must free by caller
// params: agent->state+1, agent->info.dev_id
struct mg_str gen_rpc_request(struct mg_mgr *mgr, struct agent *agent) {
    struct controller_private *priv = (struct controller_private *)mgr->userdata;
    const char *ret = NULL;
    struct mg_str request = MG_NULL_STR;
    lua_State *L = luaL_newstate();

    luaL_openlibs(L);

    if ( luaL_dofile(L, priv->cfg.opts->callback_lua) ) {
        MG_ERROR(("lua dofile %s failed, agent: %s, state: %d", priv->cfg.opts->callback_lua, agent->info.dev_id, agent->state+1));
        goto done;
    }

    lua_getfield(L, -1, "gen_rpc_request");
    if (!lua_isfunction(L, -1)) {
        MG_ERROR(("method gen_rpc_request is not a function"));
        goto done;
    }

    lua_pushinteger(L, agent->state+1);
    lua_pushstring(L, agent->info.dev_id);

    if (lua_pcall(L, 2, 1, 0)) {//two param, one return values, zero error func
        MG_ERROR(("callback failed"));
        goto done;
    }

    ret = lua_tostring(L, -1);
    if (!ret) {
        MG_ERROR(("lua call no ret"));
        goto done;
    }

    //MG_INFO(("ret: %s", ret));

    request = mg_strdup(mg_str(ret));

done:
    if (L)
        lua_close(L);

    return request;
}

// call callback lua function handle_rpc_response, return 0 if success, other if failed
// params: agent->state, agent->info.dev_id, response
struct handle_result handle_rpc_response(struct mg_mgr *mgr, struct agent *agent, struct mg_str response) {
    struct controller_private *priv = (struct controller_private *)mgr->userdata;
    const char *ret = NULL;
    cJSON *root = NULL, *code = NULL, *next_state = NULL, *next_state_stay = NULL;
    struct handle_result result = {
        .code = -1,
        .next_state = -1,
        .next_state_stay = -1,
    };
    lua_State *L = luaL_newstate();
    char *resp = mg_mprintf("%.*s", (int)response.len, response.ptr);

    luaL_openlibs(L);

    if ( luaL_dofile(L, priv->cfg.opts->callback_lua) ) {
        MG_ERROR(("lua dofile %s failed, agent: %s, state: %d", priv->cfg.opts->callback_lua, agent->info.dev_id, agent->state));
        goto done;
    }

    lua_getfield(L, -1, "handle_rpc_response");
    if (!lua_isfunction(L, -1)) {
        MG_ERROR(("method handle_rpc_response is not a function"));
        goto done;
    }

    lua_pushinteger(L, agent->state);
    lua_pushstring(L, agent->info.dev_id);
    lua_pushstring(L, resp);

    if (lua_pcall(L, 3, 1, 0)) {//three params, one return values, zero error func
        MG_ERROR(("callback failed"));
        goto done;
    }

    ret = lua_tostring(L, -1);
    if (!ret) {
        MG_ERROR(("lua call no ret"));
        goto done;
    }

    MG_INFO(("agent: %s, state: %d, ret: %s", agent->info.dev_id, agent->state, ret));
    root = cJSON_Parse(ret);
    code = cJSON_GetObjectItem(root, "code");
    if (cJSON_IsNumber(code) ) {
       result.code = (int)cJSON_GetNumberValue(code);
    }
    next_state = cJSON_GetObjectItem(root, "next_state");
    if (cJSON_IsNumber(next_state) ) {
       result.next_state = (int)cJSON_GetNumberValue(next_state);
    }
    next_state_stay = cJSON_GetObjectItem(root, "next_state_stay");
    if (cJSON_IsNumber(next_state_stay) ) {
       result.next_state_stay = (int)cJSON_GetNumberValue(next_state_stay);
    }

done:
    if (L)
        lua_close(L);
    if (resp)
        free(resp);
    if (root)
        cJSON_Delete(root);

    return result;
}