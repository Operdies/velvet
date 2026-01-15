---@class vv
local vv = {
  arrange_group_name = 'velvet.arrange',
  inspect = require('velvet.inspect'),
  --- @type velvet.api
  api = {},
  --- @type velvet.options
  options = {},
  --- Write string to the velvet log
  --- @param text string the string to log
  print = function(text) end,
  events = require('velvet.events'),
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
