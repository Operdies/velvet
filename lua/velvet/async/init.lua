--- The async API is a coroutine based implementation of velvet's event system, enabling linear control flow.

local autogen = require('velvet.async.autogen')
local known_events = autogen[1]
local M = autogen[2]

--[[
NOTE: It is easy to leak memory in the async API because we store a lot of gc handles.
To alleviate this, we use weaktables.

In general:
While a coroutine is running, we don't need to keep any GC handles.
While the coroutine is not running, ideally the only handle should be the value in
|sequence_callbacks|. When a coroutine is resumed, the entry is cleared from |sequence_callbacks|,
meaning the only handle is now the fact that it is running, and so forth.
--]]
local function make_weaktable() return setmetatable({}, { __mode = 'k' }) end

--- @type table<integer, fun(velvet.async.event_registration, velvet.async.wait.result )>
--- mapping from an integer handle to a resolve callback.
local sequence_callbacks = {}
local registered_waits = {}

local co_to_seq = make_weaktable()
local co_defer = make_weaktable()
local deferring = make_weaktable()
local co_result = make_weaktable()

-- Monotonically increasing sequence number used to invalidate multi-waits
local sequence = 1

--- @return table<string, string|boolean> seen known events
function M.get_observed_events()
  return vv.deepcopy(known_events)
end

--- Resolve all defers for |co|
--- This is called when |co| completes.
local function exec_defer(co)
  local defer = co_defer[co]
  if defer then
    -- ensure no new wait() and defer() calls are made on this thread during defer
    deferring[co] = true
    -- Ensure co_defer is nilled in case a defer calls M.cancel()
    -- Further defer calls will now error().
    co_defer[co] = nil
    for i = #defer, 1, -1 do
      local fn = defer[i]
      local ok, err = xpcall(fn, debug.traceback)
      if not ok then
        printerr(("Unhandled error in coroutine defer: %s"):format(err), 'error')
      end
    end
    deferring[co] = nil
  end
end

--- Execute |f| as a coroutine.
--- @param f fun(...): ...
--- @param ... any arguments passed to f
--- @return thread co the coroutine executing |f|. Can be cancelled with M.cancel()
function M.run(f, ...)
  if type(f) ~= 'function' then error(string.format("Bad argument #1 (function expected, got %s)", type(f))) end
  local args = {...}
  local parent = setmetatable({ coroutine.running() }, { __mode = 'kv' })
  local get_print = function()
    if parent and parent[1] then
      return COROUTINE_PRINT[parent[1]]
    end
  end

  local co = coroutine.create(function()
    co_defer[coroutine.running()] = {}
    if get_print() then
      COROUTINE_PRINT[coroutine.running()] = function(stream, ...)
        local parent_print = get_print()
        if parent_print then
          -- if the parent print is still set, use that.
          parent_print(stream, ...)
        else
          -- otherwise, unset the print function and print normally.
          -- this happens if the parent process exits while child
          -- coroutines are still running.
          COROUTINE_PRINT[coroutine.running()] = nil
          -- discard stream
          print(...)
        end
      end
    end
    local result = { xpcall(f, debug.traceback, table.unpack(args)) }
    co_result[coroutine.running()] = result
    exec_defer(coroutine.running())
  end)
  coroutine.resume(co)
  return co
end

--- Cancel all continuations for |co| and trigger deferred actions.
--- @param co thread the thread to cancel
function M.cancel(co)
  local seq = co_to_seq[co]
  if seq then
    co_to_seq[co] = nil
    sequence_callbacks[seq] = nil
  end
  co_result[co] = { false, 'canceled' }
  exec_defer(co)
end

local function defer_on(co, defer)
  if deferring[co] then error("Cannot add new defers during defer.") end
  assert(type(defer) == 'function', string.format('Bad argument #1 (function expected, got %s)', type(defer)))
  local defers = co_defer[co] or error("Provided coroutine is not managed by vv.async.")
  defers[#defers + 1] = defer
end

--- defer a function which runs when the current coroutine completes or is cancelled.
--- @param defer fun() deferred action
function M.defer(defer)
  defer_on(coroutine.running(), defer)
end

local function resolve(name, data)
  -- capture the current sequence number and ensure we don't resolve anything higher.
  -- Otherwise a waiter() invocation can trigger on the currently processing event.
  local current_sequence = sequence
  local function resolve_table(tbl)
    for seq, regs in pairs(tbl) do
      -- tbl is indexed both by integer keys and string keys.
      -- In this loop we are only interested in integer keys since string keys refer to nested tables.
      if type(seq) ~= 'number' or seq > current_sequence then goto next_waiter end
      local waiter = sequence_callbacks[seq]
      -- if waiter is not set, this event is stale, so we can remove it right away and move on.
      if not waiter then
        tbl[seq] = nil
        goto next_waiter
      end
      for _, reg in ipairs(regs) do
        local is_match = true
        local wait_result = { name = name, data = data }
        if reg.when then
          local ok, result = xpcall(reg.when, debug.traceback, reg, wait_result)
          if not ok then
            printerr(string.format("Unhandled error during when(%s): %s", name, result))
            is_match = false
          else
            is_match = result
          end
        end
        if is_match then
          sequence_callbacks[seq] = nil
          waiter(reg, wait_result)
          tbl[seq] = nil
          break
        end
      end
      ::next_waiter::
    end
  end

  if not known_events[name] then known_events[name] = true end
  local segments = {}
  for segment in name:gmatch('[^.]+') do
    segments[#segments + 1] = segment
  end

  local function recursive_resolve(level, word, ...)
    local leaf = select('#', ...) == 0
    local any = level['**']
    if any then resolve_table(any) end
    local star = level['*']
    local match = level[word]

    if leaf then
      if star then resolve_table(star) end
      if match then resolve_table(match) end
    else
      if star then recursive_resolve(star, ...) end
      if match then recursive_resolve(match, ...) end
    end
  end

  recursive_resolve(registered_waits, table.unpack(segments))
end

local e = require('velvet.events').create_group('velvet.async', true)
e['**'] = resolve

--- @class velvet.async.conditional_event
--- @field event velvet.async.event|string event
--- @field when fun(registration: velvet.async.event_registration, result: velvet.async.wait.result): boolean predicate function

--- @alias velvet.async.event_registration velvet.async.event|velvet.async.conditional_event|thread|'*'|'**'|string

--- @class velvet.async.wait.result
--- @field name velvet.async.event|string the name of the raised event
--- @field data any the event args

local function timeout_callback(co, timeout, seq)
  return vv.api.schedule_after(timeout, function()
    -- if sequence_callbacks was unset, that means this coroutine was cancelled.
    if not sequence_callbacks[seq] then return end
    sequence_callbacks[seq] = nil
    coroutine.resume(co, nil, 'timeout')
  end)
end

local function defer_callback(co, trd, seq)
  defer_on(trd, function()
    if not sequence_callbacks[seq] then return end
    sequence_callbacks[seq] = nil
    coroutine.resume(co, trd, co_result[trd])
  end)
end

local function resolve_callback(co, timeout)
return function(registration, result)
    if timeout then vv.api.schedule_cancel(timeout) end
    coroutine.resume(co, registration, result)
  end
end

--- Wait for all registrations in |events| to fire, or |timeout|.
--- @param events table<any, velvet.async.event_registration> One or more events to wait for.
--- @param timeout? integer optional timeout
--- @return table<any, velvet.async.wait.result> the result of each wait operation, with the same keys as |events|
function M.wait_all(events, timeout)
  -- shallow copy to avoid mutating the user provided table
  local inputs = {}; for k, v in pairs(events) do inputs[k] = v end
  local outputs = {}
  local deadline = timeout and (vv.api.get_current_tick() + timeout) or nil
  while next(inputs) do
    local args = {}
    local lookup = {}
    for i, v in pairs(inputs) do args[#args+1] = v; lookup[v] = i; end
    local t = deadline and (deadline - vv.api.get_current_tick()) or nil
    local reg, evt = vv.async.wait(table.unpack(args), t)
    if reg then
      local key = lookup[reg]
      inputs[key] = nil
      outputs[key] = evt
    else
      -- timeout
      return outputs
    end
  end
  return outputs
end

--- @param co thread
--- @return boolean success
--- @return any result
--- @return any ...
function M.wait_for_coroutine(co)
  local _, tbl = M.wait(co)
  return table.unpack(tbl)
end

--- Wait for one of the events to fire, or |timeout|.
--- @param ... velvet.async.event_registration|integer One or more events to wait for. A number can optionally be parsed which will be interpreted as the timeout in milliseconds.
--- @return velvet.async.event_registration, velvet.async.wait.result The argument which resolved the wait, and the wait result, or 'timeout' on timeout
function M.wait(...)
  local timeout = nil
  local co = coroutine.running()
  if deferring[co] then error("Cannot wait() during defer.") end
  sequence = sequence + 1
  -- local capture to preserve the sequence number
  local seq = sequence

  local compatible_types = { number = true, string = true, table = true, thread = true }
  local raw_args = {...}
  if #raw_args == 0 then error("No events specified.") end
  local timeout_value = nil
  local args = {}
  for i, evt in ipairs(raw_args) do
    local tp = type(evt)
    if not compatible_types[tp] then
      error(("Bad argument #%d (number, string, coroutine, or table expected)"):format(i))
    end
    if tp == 'thread' then
      -- if the coroutine completed, or is not running, return immediately.
      if co_result[evt] then return evt, co_result[evt] end
      if not co_defer[evt] then 
        ---@diagnostic disable-next-line: return-type-mismatch
        return evt, nil
      end
    elseif tp == 'number' then
      if math.type(evt) ~= 'integer' then
        error(("Bad argument #%d (integer expected, got number)"):format(i))
      end
      timeout_value = timeout_value and math.min(timeout_value, evt) or evt
    end
    if tp ~= 'number' then
      args[#args + 1] = evt
    end
  end

  if timeout_value then
    timeout = timeout_callback(co, timeout_value, seq)
  end

  co_to_seq[co] = seq
  sequence_callbacks[seq] = resolve_callback(co, timeout)

  for idx, evt in ipairs(args) do
    if type(evt) == 'thread' then
      defer_callback(co, evt, seq)
    elseif type(evt) == 'string' or type(evt) == 'table' then
      local event = evt
      if type(evt) == 'table' then
        assert(type(evt.event) == 'string', ("Bad argument #%d: bad field 'event' (string expected, got %s)"):format(idx, type(evt.event)))
        if evt.when ~= nil then
          assert(type(evt.when) == 'function', ("Bad argument #%d: bad field 'when' (function expected, got %s)"):format(idx, type(evt.when)))
        end
        event = evt.event
      end
      assert(type(event) == 'string')
      local tbl = registered_waits
      for segment in event:gmatch('[^.]+') do
        local sub = tbl[segment]
        if not sub then
          sub = {}; tbl[segment] = sub
        end
        tbl = sub
      end
      if tbl == registered_waits then 
        error(('Bad argument #%d (malformed event specifier %s)'):format(idx, event))
      end
      if tbl[seq] then
        -- this event has multiple registrations on the same event.
        -- there is nothing wrong with this since the registrations can have
        -- different |when| triggers, but we need to handle this by chaining
        -- the registrations.
        table.insert(tbl[seq], evt)
      else
        tbl[seq] = { evt }
      end
    else
      error(('bad argument #%d (string|number expected, got %s)'):format(idx, type(evt)))
    end
  end

  return coroutine.yield()
end

--- Returns an iterator which yields whenever an event in |...| is fired. Terminates on timeout if specified.
--- @param ... velvet.async.event_registration|integer One or more events to stream. A number can optionally be parsed which will be interpreted as the timeout in milliseconds.
--- @return fun(): velvet.async.event_registration, velvet.async.wait.result Iterator which streams the input events
function M.stream(...)
  local args = {...}
  return function()
    local registration, result = M.wait(table.unpack(args))
    return registration or 'timeout', result
  end
end

return M
