---@class event_group a handle to a group of event handlers
---@field id integer the group handle

local sequence = 1
local function next_id()
  sequence = sequence + 1
  return sequence
end

local function dispatch_event(event_name, handler, ...)
  local ok, err = xpcall(handler, debug.traceback, ...)
  if not ok and event_name ~= 'system_message' then
    printerr(string.format("Unhandled error in event handler (event %s): %s",
      event_name, err))
  end
end

local gsub_cache = {}

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
    local lookup_name = gsub_cache[event_name]
    if lookup_name == nil then
      lookup_name = event_name:gsub('%.', '_')
      gsub_cache[event_name] = lookup_name
    end
    for _, id in pairs(event_groups) do
      local group_func_table = event_handlers[id]
      if group_func_table then
        local handler = group_func_table['**']
        local include_event_name = true
        if not handler then
          handler = group_func_table[event_name] or group_func_table[lookup_name]
          include_event_name = false
        end
        if handler then
          local d = vv.deepcopy(data)
          -- include_event_name:
          --   true  -> handler(event_name, data)
          --   false -> handler(data, nil)
          local arg1 = include_event_name and event_name or d
          local arg2 = include_event_name and d or nil
          vv.async.run(dispatch_event, event_name, handler, arg1, arg2)
        end
      end
    end
    if event_name == 'pre_reload' then
      vv.events.emit('pre_reload.late', data)
    end
  end
}

return events
