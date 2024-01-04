#include <lualib.h>
#include <lauxlib.h>
#include <iot/mongoose.h>
#include <iot/cJSON.h>
#include "apmgr.h"
#include "ap.h"

// call callback lua function gen_rpc_request, return request, must free by caller
// params: ap->state+1, ap->info.dev_id
struct mg_str gen_rpc_request(struct mg_mgr *mgr, struct ap *ap) {
    struct apmgr_private *priv = (struct apmgr_private *)mgr->userdata;
    const char *ret = NULL;
    struct mg_str request = MG_NULL_STR;
    lua_State *L = luaL_newstate();

    luaL_openlibs(L);

    if ( luaL_dofile(L, priv->cfg.opts->callback_lua) ) {
        MG_ERROR(("lua dofile %s failed, ap: %s, state: %d", priv->cfg.opts->callback_lua, ap->info.dev_id, ap->state+1));
        goto done;
    }

    lua_getfield(L, -1, "gen_rpc_request");
    if (!lua_isfunction(L, -1)) {
        MG_ERROR(("method gen_rpc_request is not a function"));
        goto done;
    }

    lua_pushinteger(L, ap->state+1);
    lua_pushstring(L, ap->info.dev_id);

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
// params: ap->state, ap->info.dev_id, response
int handle_rpc_response(struct mg_mgr *mgr, struct ap *ap, struct mg_str response) {
    struct apmgr_private *priv = (struct apmgr_private *)mgr->userdata;
    const char *ret = NULL;
    cJSON *root = NULL, *code = NULL;
    int ret_code = -1;
    lua_State *L = luaL_newstate();
    char *resp = mg_mprintf("%.*s", (int)response.len, response.ptr);

    luaL_openlibs(L);

    if ( luaL_dofile(L, priv->cfg.opts->callback_lua) ) {
        MG_ERROR(("lua dofile %s failed, ap: %s, state: %d", priv->cfg.opts->callback_lua, ap->info.dev_id, ap->state));
        goto done;
    }

    lua_getfield(L, -1, "handle_rpc_response");
    if (!lua_isfunction(L, -1)) {
        MG_ERROR(("method handle_rpc_response is not a function"));
        goto done;
    }

    lua_pushinteger(L, ap->state);
    lua_pushstring(L, ap->info.dev_id);
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

    MG_INFO(("ap: %s, state: %d, ret: %s", ap->info.dev_id, ap->state, ret));
    root = cJSON_Parse(ret);
    code = cJSON_GetObjectItem(root, "code");
    if (cJSON_IsNumber(code) ) {
       ret_code = (int)cJSON_GetNumberValue(code);
    }

done:
    if (L)
        lua_close(L);
    if (resp)
        free(resp);
    if (root)
        cJSON_Delete(root);

    return ret_code;
}