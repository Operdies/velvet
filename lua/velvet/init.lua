---@class vv
local vv = {
  inspect = require('velvet.inspect'),
  --- @type velvet.api
  api = {},
  --- @type velvet.keymap
  keymap = {},
  --- @type velvet.options 
  options = {}
}

-- vv.api is populated in C. Here we use a metatable to direct 
-- vv.options reads and assignments to the vv_api accessors
vv.options = setmetatable(vv.options, {
  __index = function(_, k)
    return vv.api["get_" .. k]({})
  end,
  __newindex = function(_, k, v)
    return vv.api["set_" .. k](v, {})
  end,
})

return vv
