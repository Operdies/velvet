local M = {}

local function dispatch_event(event_name, handler, data)
  local ok, err = xpcall(handler, debug.traceback, data)
  if not ok and event_name ~= 'system_message' then
    printerr(string.format("Unhandled error in event handler (event %s): %s",
      event_name, err))
  end
end

--- @type velvet.api.event_handler
local async_emitter = nil
--- @type table<string, velvet.api.event_handler>
local event_groups = {}
--- @type table<string, string>
local gsub_cache = {}

---Create a new event group. An event group can be cleared and unregistered together
---@param group_name string the name of the new group.
---@param clear boolean if true, existing event handlers with the same key will be cleared. This is useful for automatically unregistering a handler when reloading your config.
---@return velvet.api.event_handler
---@nodiscard
function M.create_group(group_name, clear)
  local group = event_groups[group_name]
  if group == nil or clear then
    group = {}
    event_groups[group_name] = group
  end

  if group_name == 'velvet.async' then
    async_emitter = group
  end

  return group
end

--- Delete the event group |group|
--- @param event_handler velvet.api.event_handler
function M.delete_group(event_handler)
  event_groups[event_handler] = nil
end

--- @param event_name string the raised event
--- @param data any event data
--- @package
function M.emit(event_name, data)
  local lookup_name = gsub_cache[event_name]
  if lookup_name == nil then
    lookup_name = event_name:gsub('%.', '_')
    gsub_cache[event_name] = lookup_name
  end

  -- event_groups could be modified during event dispatch,
  -- so we extract the groups first and then dispatch them
  local groups = {}
  for k, v in pairs(event_groups) do
    groups[k] = v
  end

  -- we need to run the async emitter specifically synchronously
  -- because it will internally yield the emitter thread if it is currently dispatching another event.
  -- this will never be triggered for internal events (emitted from C) because it flushes its dispatch queue
  -- before returning. This mainly causes problems when an event handler re-emits another event via events.emit().
  async_emitter['**'](event_name, data)

  for _, group in pairs(groups) do
    local handler = group[event_name] or group[lookup_name]
    if handler then
      vv.async.run(dispatch_event, event_name, handler, vv.deepcopy(data))
    end
  end

  -- it would be problematic if pre_reload.late or pre_render.late did not get emitted immediately (because the async emitter was yielded)
  -- but that should not be possible because the this call frame is directly above the C context.
  if event_name == 'pre_reload' then
    M.emit('pre_reload.late', data)
  elseif event_name == 'pre_render' then
    M.emit('pre_render.late', data)
  end
end

return M
