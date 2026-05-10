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
  --- @param data any event data
  emit = function(event_name, data)
    local lookup_key = event_name:gsub('[.]', '_')
    for _, id in pairs(event_groups or {}) do
      local group_func_table = event_handlers[id] or {}
      local handler = group_func_table[lookup_key] or group_func_table[event_name]
      local include_event_name = false
      if not handler then
        handler = group_func_table["**"]
        include_event_name = true
      end
      if handler then
        vv.async.run(function()
          local d = vv.deepcopy(data)
          local function get_args() if include_event_name then return event_name, d else return d end end
          local ok, err = xpcall(handler, debug.traceback, get_args())
          if not ok and event_name ~= 'system_message' then
            printerr(string.format("Unhandled error in event handler (event %s): %s",
              event_name, err))
          end
        end)
      end
    end
    if event_name == 'pre_reload' then
      vv.events.emit('pre_reload.late', data)
    end
  end
}

return events
