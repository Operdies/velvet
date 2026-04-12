---@class event_group a handle to a group of event handlers
---@field id integer the group handle

local sequence = 1
local function next_id()
  sequence = sequence + 1
  return sequence
end

---@class events
local event_handlers = {}
local event_groups = {}
local events = {
  ---Create a new event group. An event group can be cleared and unregistered together
  ---@param group_name string the name of the new group.
  ---@param clear boolean if true, existing event handlers with the same key will be cleared. This is useful for automatically unregistering a handler when reloading your config.
  ---@return velvet.api.event_handler
  ---@nodiscard
  create_group = function(group_name, clear)
    local id = event_groups[group_name] or next_id()
    event_groups[group_name] = id
    local group = { id = id, name = group_name }
    if clear then
      event_handlers[group.id] = group
      return group
    else
      if not event_handlers[id] then event_handlers[id] = group end
      return event_handlers[id]
    end
  end,

  --- Delete the event group |group|
  --- @param event_handler velvet.api.event_handler
  delete_group = function(event_handler)
    event_handlers[event_handler.id] = nil
  end,

  --- @param event_name string the raised event
  --- @param ... any depends on the event
  emit_event = function(event_name, ...)
    local args = table.pack(...)
    for _, id in pairs(event_groups or {}) do
      local group_func_table = event_handlers[id] or {}
      local handler = group_func_table[event_name]
      local prefix = false
      if not handler then
        handler = group_func_table["any"]
        prefix = true
      end
      if handler then
        local error_handler = function(e)
          -- prevent recursion if message handlers have errors
          if event_name ~= 'system_message' then
            vv.log(("Unhandled error in event handler. (event %s)"):format(event_name), 'error')
            vv.log(debug.traceback(e, 2), 'debug')
          end
          return e
        end
        coroutine.wrap(function() 
          if prefix == true then
            xpcall(handler, error_handler, event_name, table.unpack(vv.deepcopy(args), 1, args.n)) 
          else
            xpcall(handler, error_handler, table.unpack(vv.deepcopy(args), 1, args.n)) 
          end
        end)()
      end
    end
  end
}

return events
