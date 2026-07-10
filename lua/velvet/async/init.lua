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
--- @param mode 'k'|'v'
local function make_weaktable(mode) return setmetatable({}, { __mode = mode }) end

--- @alias velvet.async.resolve fun(reg: velvet.async.event_registration, result: velvet.async.wait.result)
--- @alias velvet.async.resolve_table table<integer, velvet.async.resolve>

--- @class velvet.async.waiter_registry 
--- @field [integer] velvet.async.event_registration[]
--- @field [string] velvet.async.waiter_registry

--- mapping from an integer handle to a resolve callback.
--- @type velvet.async.resolve_table
local sequence_callbacks = {}
--- @type velvet.async.waiter_registry
local waiter_registry = {}

--- Maps a coroutine to a sequence number.
--- A sequence number is an integer key in the sequence_callbacks table.
--- When there are no more strong references to a coroutine, the entry is dropped from this table.
local co_to_seq = make_weaktable('k')
--- Maps a coroutine object to zero or more deferred functions and parameters.
--- Deferred functions are executed in reverse order when a coroutine completes,
--- or when it is canceled.
local co_defer = make_weaktable('k')
--- Lookup table indicating whether a coroutine is currently deferring.
--- We need to track this information in order to reject further defer operations
--- before the coroutine is cleared.
--- @type table<thread, boolean>
local deferring = make_weaktable('k')
--- Lookup table mapping a coroutine to its result.
--- co_result uses the same semantics as pcall();
--- The first value is a status boolean indicating success,
--- and the remaining values represent either an error
--- or zero or more return values.
local co_result = make_weaktable('k')
--- Lookup table mapping event_source objects to their waitings;
--- This is similar to registered_waits, except it is using weak keys,
--- which means waiters can be collected if the event source is no longer reachable.
--- @type table<velvet.async.event_source, velvet.async.event_registration[]>
local event_source_waiter_registry = make_weaktable('k')
--- weak-valued table containing the currently deferring coroutine, or nil
--- @type [thread?]
local currently_deferring_coroutine = make_weaktable('v')

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
      local df = defer[i]

      -- store a reference to the currently deferring coroutine.
      -- this makes it possible for (internal) defer operations
      -- to access the deferring coroutine for e.g. lookup tables
      -- without keeping a long-lived strong reference to its associated resources.
      local tmp = currently_deferring_coroutine[1]
      currently_deferring_coroutine[1] = co
      local ok, err = xpcall(df[1], debug.traceback, table.unpack(df, 2, #df))
      currently_deferring_coroutine[1] = tmp

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
  local args = { ... }
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

local function defer_on(co, defer, ...)
  if deferring[co] then error("Cannot add new defers during defer.") end
  assert(type(defer) == 'function', string.format('Bad argument #1 (function expected, got %s)', type(defer)))
  local defers = co_defer[co] or error("Provided coroutine is not managed by vv.async.")
  defers[#defers + 1] = { defer, ... }
end

--- defer a function which runs when the current coroutine completes or is cancelled.
--- @param defer fun() deferred action
--- @param ... any parameters passed to |defer|
function M.defer(defer, ...)
  defer_on(coroutine.running(), defer, ...)
end

local function resolve(event, data)
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
        local wait_result = { event = event, data = data }
        if reg.when then
          local ok, result = xpcall(reg.when, debug.traceback, reg, wait_result)
          if not ok then
            printerr(string.format("Unhandled error during when(%s): %s",
              type(event) == 'string' and event or 'event_source', result))
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

  if type(event) == 'table' then
    local waiters = event_source_waiter_registry[event]
    if waiters then resolve_table(waiters) end
    return
  end

  if not known_events[event] then known_events[event] = true end
  local segments = {}
  for segment in event:gmatch('[^.]+') do
    segments[#segments + 1] = segment
  end

  local function recursive_resolve(wait_table, word, ...)
    local leaf = select('#', ...) == 0
    local any = wait_table['**']
    if any then resolve_table(any) end
    local star = wait_table['*']
    local match = wait_table[word]

    if leaf then
      if star then resolve_table(star) end
      if match then resolve_table(match) end
    else
      if star then recursive_resolve(star, ...) end
      if match then recursive_resolve(match, ...) end
    end
  end

  recursive_resolve(waiter_registry, table.unpack(segments))
end

local e = require('velvet.events').create_group('velvet.async', true)
e['**'] = resolve

--- @class velvet.async.event_source
local EventSource = {}
EventSource.__index = EventSource

--- @param event any
function EventSource:emit(event)
  assert(getmetatable(self) == EventSource, "Bad argument #1 (event_source expected)")
  resolve(self, event)
end

--- @return velvet.async.wait.result
function EventSource:wait()
  assert(getmetatable(self) == EventSource, "Bad argument #1 (event_source expected)")
  local _, evt = M.wait(self)
  return evt.data
end

--- Create an event source. The event source can be signaled with an object, and awaited with async.wait()
--- @return velvet.async.event_source src
function M.event_source()
  local instance = setmetatable({}, EventSource)
  event_source_waiter_registry[instance] = {}
  return instance
end

--- @class velvet.async.conditional_event
--- @field event velvet.async.event|velvet.async.event_source|string event
--- @field when fun(registration: velvet.async.event_registration, result: velvet.async.wait.result): boolean predicate function

--- @alias velvet.async.event_registration velvet.async.event|velvet.async.event_source|velvet.async.conditional_event|thread|'*'|'**'|string

--- @class velvet.async.wait.result
--- @field event velvet.async.event|string|velvet.async.event_source the raised event
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

local function is_event_source(evt)
  return type(evt) == "table" and (
    getmetatable(evt) == EventSource or
    getmetatable(evt.event) == EventSource)
end

local function emit_coroutine_result(evt)
  evt:emit(co_result[currently_deferring_coroutine[1]])
end

--- Wait for all registrations in |events| to fire, or |timeout|.
--- @param events table<any, velvet.async.event_registration> One or more events to wait for.
--- @param timeout? integer optional timeout
--- @return table<any, velvet.async.wait.result> the result of each wait operation, with the same keys as |events|
function M.wait_all(events, timeout)
  -- shallow copy to avoid mutating the user provided table
  local result = {}
  local args = {}
  local missing = 0

  if timeout ~= nil then
    assert(math.type(timeout) == 'integer', string.format("Bad argument #2 (integer expected, got %s)", type(timeout)))
    args[1] = timeout
  end

  for key, reg in pairs(events) do
    local inner_when, found, event
    event = reg

    if is_event_source(reg) then
      event = reg.event or reg
      inner_when = reg.when
    elseif type(reg) == 'table' then
      event = reg.event
      inner_when = reg.when
    elseif type(reg) == 'thread' then
      -- threads do not support 'when' clauses, so we must wrap thread
      -- completions in an event source for the resolve handler to function.
      -- This is not so bad because event sources are very lightweight.
      event = M.event_source()
      defer_on(reg, emit_coroutine_result, event)
    end

    local when = function(_, evt)
      if found then return false end                                   -- already recorded
      if inner_when and not inner_when(reg, evt) then return false end -- value rejected
      found = true
      result[key] = evt
      missing = missing - 1
      return missing == 0 -- resolve wait_all when no events are missing
    end

    args[#args + 1] = { event = event, when = when }
    missing = missing + 1
  end
  if missing == 0 then return {} end
  vv.async.wait(table.unpack(args))
  return result
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
  local raw_args = { ... }
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
    elseif is_event_source(evt) then
      local event_waiters = event_source_waiter_registry[evt.event or evt]
      event_waiters[seq] = { evt }
    elseif type(evt) == 'string' or type(evt) == 'table' then
      local event = evt
      if type(evt) == 'table' then
        assert(type(evt.event) == 'string',
          ("Bad argument #%d: bad field 'event' (string expected, got %s)"):format(idx, type(evt.event)))
        assert(evt.when == nil or type(evt.when) == 'function',
          ("Bad argument #%d: bad field 'when' (function expected, got %s)"):format(idx, type(evt.when)))
        event = evt.event
      end
      assert(type(event) == 'string')
      local wait_table = waiter_registry
      for segment in event:gmatch('[^.]+') do
        local sub_table = wait_table[segment]
        if not sub_table then
          sub_table = {}; wait_table[segment] = sub_table
        end
        wait_table = sub_table
      end
      if wait_table == waiter_registry then
        error(('Bad argument #%d (malformed event specifier %s)'):format(idx, event))
      end
      if wait_table[seq] then
        -- this event has multiple registrations on the same event.
        -- there is nothing wrong with this since the registrations can have
        -- different |when| triggers, but we need to handle this by chaining
        -- the registrations.
        table.insert(wait_table[seq], evt)
      else
        wait_table[seq] = { evt }
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
  local args = { ... }
  return function()
    local registration, result = M.wait(table.unpack(args))
    return registration or 'timeout', result
  end
end

return M
