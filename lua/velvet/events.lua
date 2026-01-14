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
  screen = { resized = "screen_resized" },
  window = { created = "window_created", removed = "window_removed", moved = "window_moved", resized = "window_resized" },

  ---Create a new event group. An event group can be cleared and unregistered together
  ---@param group_name string the name of the new group.
  ---@param clear boolean if true, existing subscriptions with the same key will be cleared.
  ---@return event_group
  create_group = function(group_name, clear)
    local id = event_groups[group_name] or next_id()
    local group = { id = id }
    event_groups[group_name] = group.id
    if clear then 
      event_handlers[group.id] = nil 
    end
    return group
  end,

  --- Delete the event group |group|
  --- @param group event_group
  delete_group = function(group)
    event_handlers[group.id] = nil
  end,

  --- Subscribe |group| to event |event| with callback |func|
  --- @param group event_group
  --- @param event_name string the event to subscribe to
  --- @param func function the callback
  subscribe = function(group, event_name, func)
    local group_func_table = event_handlers[group.id] or {}
    group_func_table[event_name] = func
    event_handlers[group.id] = group_func_table
  end,

  --- Internal use
  emit_event = function(event_name, data)
    for name, id in pairs(event_groups or {}) do
      local group_func_table = event_handlers[id] or {}
      if group_func_table[event_name] then 
        local ok, err = pcall(group_func_table[event_name], data)
        if not ok then 
          print(vv.inspect({
            event = event_name,
            data = data,
            handler_name = name,
            group_id = id,
            func = group_func_table[event_name],
            error = err
          }))
        end
      end
    end
  end
}

return events
