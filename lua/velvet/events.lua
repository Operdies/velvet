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

  --- @package
  --- don't use this
  --- @param event_name string the raised event
  --- @param ... any depends on the event
  emit_event = function(event_name, ...)
    local data = ...
    if event_name == 'window_on_key' then 
      -- The C api does not set a name for regular characters, such as latin letters or
      -- normal letters from other scripts. For ease of use, we convert the codepoints
      -- of such characters to a string containing the character.
      if data.key.name == nil then
        local cp = data.key.codepoint
        -- if alternate codepoint is set, the character is shifted, and we should
        -- use the shifted key for the name instead.
        if data.key.alternate_codepoint ~= 0 then
          cp = data.key.alternate_codepoint
        end
        name = utf8.char(cp)
      end
    end
    for name, id in pairs(event_groups or {}) do
      local group_func_table = event_handlers[id] or {}
      if group_func_table[event_name] then 
        local ok, err = pcall(group_func_table[event_name], ...)
        if not ok then 
          -- enrich the error event with detailed information about the handler
          error(vv.inspect({
            event = event_name,
            data = data,
            handler_name = name,
            error = err
          }))
        end
      end
    end
  end
}

return events
