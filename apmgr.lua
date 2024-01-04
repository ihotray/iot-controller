local cjson = require 'cjson.safe'
local M = {}

---state: number, state of ap
---id: string, id of ap
---return: string, request
---state example:
---1: ap is connected
---2: ap get system info..., if timeout, back to state 1
---3: ap get system info success
---4: ap get system status..., if timeout, back to state 3
---5: ap get system status success

M.gen_rpc_request = function (state, id)
    -- gen request with state and ap id

    if state == 2 then
        return '{"method": "call","param": ["system/info", "get_info"]}'
    end

    if state == 4 then
        return '{"method": "call","param": ["system/info", "get_status"]}'
    end

    return '{"method": "call","param": ["system/info", "get_info"]}'
end


M.handle_rpc_response = function (state, id, response)
    -- handle response with state and ap id

    return cjson.encode({code = 0})
end

return M