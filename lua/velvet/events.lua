---@class event_group a handle to a group of event handlers
---@field id integer the group handle

---@class events
local event_handlers = {}
local event_groups = {}
local events = {
  screen = { resized = "screen_resized" },
  window = { created = "window_created", removed = "window_removed", moved = "window_moved", resized = "window_resized" },

  ---Create a new event group. An event group can be cleared and unregistered together
  ---@param key string the name of the new group.
  ---@param clear boolean if true, existing subscriptions with the same key will be cleared.
  ---@return event_group
  create_group = function(key, clear)
    local id = event_groups[key] or #event_groups + 1
    local group = { id = id }
    event_groups[key] = group.id
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
  --- @param event string the event to subscribe to
  --- @param func function the callback
  subscribe = function(group, event, func)
    local handler = event_handlers[group.id] or {}
    handler[event] = func
    event_handlers[group.id] = handler
  end,

  --- Internal use
  emit_event = function(event, data)
    for _, handler in pairs(event_handlers) do
      if handler[event] then pcall(handler[event], data) end
    end
  end
}

return events
