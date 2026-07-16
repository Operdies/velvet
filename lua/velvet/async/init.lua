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
--- @param mode 'k'|'v'|'kv'
local function make_weaktable(mode) return setmetatable({}, { __mode = mode }) end

--- @generic T any
--- @param obj T object to wrap
--- @return [T] wrap array containing a weak reference to |obj|
local function weakref(obj) return setmetatable({obj}, { __mode = 'v' }) end

--- @alias velvet.async.resolve fun(reg: velvet.async.event_registration, result: velvet.async.wait.result)
--- @alias velvet.async.resolve_table table<integer, velvet.async.resolve>

--- @generic T
--- @alias velvet.async.single_when<T> fun(data: T): boolean

--- @generic T
--- @alias velvet.async.generic_when<T> fun(registration: velvet.async.event_registration, event: velvet.async.wait.result): boolean

--- @class velvet.async.waiter_registry
--- @field [integer] velvet.async.event_registration[]
--- @field [string] velvet.async.waiter_registry

--- mapping from an integer handle to a resolve callback.
--- @type velvet.async.resolve_table
local sequence_callbacks = {}
--- @type velvet.async.waiter_registry
local waiter_registry = {}

--- @class velvet.async.coroutine_state
--- @field deferring? boolean true if the coroutine is currently deferring
--- @field result? any[]? set if the coroutine is completed or canceled
--- @field sequence? integer lookup key to |sequence_callbacks|
--- @field defers any[] list of deferred functions and their parameters
--- @field timeout? integer timeout token for this coroutine

--- @type table<thread, velvet.async.coroutine_state>
local co_state = make_weaktable('k')

local event_source_waiter_registry = make_weaktable('k')
--- weak-valued table containing the currently deferring coroutine, or nil
--- @type [thread?]
local currently_deferring_coroutine = make_weaktable('v')

-- Monotonically increasing sequence number used to invalidate stale waiters
local sequence = 1

--- @return table<string, string|boolean> seen known events
function M.get_observed_events()
  return vv.deepcopy(known_events)
end

local function state_cancel_timeout(state)
  if state.timeout then
    vv.api.schedule_cancel(state.timeout)
    state.timeout = nil
  end
end

--- Resolve all defers for |co|
--- This is called when |co| completes.
local function exec_defer(co)
  local state = co_state[co]
  -- ensure no new wait() and defer() calls are made on this thread during defer
  state.deferring = true

  local defers = state.defers
  state.defers = nil
  state_cancel_timeout(state)

  for i = #defers, 1, -1 do
    local defer = defers[i]

    -- store a reference to the currently deferring coroutine.
    -- this makes it possible for (internal) defer operations
    -- to access the deferring coroutine for e.g. lookup tables
    -- without keeping a long-lived strong reference to its associated resources.
    local tmp = currently_deferring_coroutine[1]
    currently_deferring_coroutine[1] = co
    local ok, err = xpcall(defer[1], debug.traceback, table.unpack(defer, 2, #defer))
    currently_deferring_coroutine[1] = tmp

    if not ok then
      printerr(("Unhandled error in coroutine defer: %s"):format(err), 'error')
    end
  end
end

local function get_co_print(co, wrapped_parent)
  return function(stream, ...)
    local parent_print = COROUTINE_PRINT[wrapped_parent[1]]
    -- if the parent print is still set, use that.
    if parent_print then
      parent_print(stream, ...)
    else
      -- otherwise, unset the print function and print normally.
      -- this happens if the parent process exits while child
      -- coroutines are still running.
      COROUTINE_PRINT[co] = nil
      print(...)
    end
  end
end

local function co_run(f, ...)
  local state = { defers = {}, deferring = false }
  local co = coroutine.running()
  co_state[co] = state
  state.result = table.pack(xpcall(f, debug.traceback, ...))
  exec_defer(co)
end

--- Execute |f| as a coroutine.
--- @param f fun(...): ...
--- @param ... any arguments passed to f
--- @return thread co the coroutine executing |f|. Can be cancelled with M.cancel()
function M.run(f, ...)
  if type(f) ~= 'function' then error(string.format("Bad argument #1 (function expected, got %s)", type(f))) end
  local co = coroutine.create(co_run)
  if COROUTINE_PRINT[coroutine.running()] then
    COROUTINE_PRINT[co] = get_co_print(co, weakref(coroutine.running()))
  end
  coroutine.resume(co, f, ...)
  return co
end

--- Cancel all continuations for |co| and trigger deferred actions.
--- @param co thread the thread to cancel
---@return boolean noerror
---@return any errorobject
function M.cancel(co)
  local state = co_state[co]
  if type(co) ~= 'thread' then
    return false, string.format("Bad argument #1 (thread expected, got %s)", type(co))
  end
  -- probably not managed by async
  if not state then return false, "provided coroutine not managed by vv.async" end
  if state.sequence then
    sequence_callbacks[state.sequence] = nil
    state.sequence = nil
  end
  state.result = { false, 'canceled' }
  exec_defer(co)

  if coroutine.status(co) == 'suspended' then
    return coroutine.close(co)
  end
  return true
end

local function defer_on(co, defer, ...)
  local state = co_state[co]
  if state.deferring then error("Cannot add new defers during defer.") end
  assert(type(defer) == 'function', string.format('Bad argument #1 (function expected, got %s)', type(defer)))
  local defers = state.defers or error("Provided coroutine is not managed by vv.async.")
  defers[#defers + 1] = { defer, ... }
end

--- defer a function which runs when the current coroutine completes or is cancelled.
--- @param defer fun() deferred action
--- @param ... any parameters passed to |defer|
function M.defer(defer, ...)
  defer_on(coroutine.running(), defer, ...)
end

--- @param wait_table velvet.async.waiter_registry
--- @param current_sequence integer
--- @param event string|velvet.async.event_source
--- @param data velvet.async.wait.result
local function resolve_table(wait_table, current_sequence, event, data)
  for seq, registrations in pairs(wait_table) do
    -- tbl is indexed both by integer keys and string keys.
    -- In this loop we are only interested in integer keys since string keys refer to nested tables.
    if type(seq) ~= 'number' or seq > current_sequence then goto next_waiter end
    local waiter = sequence_callbacks[seq]
    -- if waiter is not set, this event is stale, so we can remove it right away and move on.
    if not waiter then
      wait_table[seq] = nil
      goto next_waiter
    end
    for _, reg in ipairs(registrations) do
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
        waiter(reg, wait_result)
        wait_table[seq] = nil
        break
      end
    end
    ::next_waiter::
  end
end

--- @param current_sequence integer
--- @param event string|velvet.async.event_source
--- @param data velvet.async.wait.result
--- @param wait_table velvet.async.waiter_registry
--- @param word string
--- @param ... string
local function recursive_resolve(current_sequence, event, data, wait_table, word, ...)
  local leaf = select('#', ...) == 0
  local any = wait_table['**']
  if any then resolve_table(any, current_sequence, event, data) end
  local star = wait_table['*']
  local match = wait_table[word]

  if leaf then
    if star then resolve_table(star, current_sequence, event, data) end
    if match then resolve_table(match, current_sequence, event, data) end
  else
    if star then recursive_resolve(current_sequence, event, data, star, ...) end
    if match then recursive_resolve(current_sequence, event, data, match, ...) end
  end
end

--- @param event string|velvet.async.event_source
--- @param data velvet.async.wait.result
local function resolve(event, data)
  if type(event) == 'table' then
    local waiters = event_source_waiter_registry[event]
    if waiters then resolve_table(waiters, sequence, event, data) end
  elseif type(event) == 'string' then
    if not known_events[event] then known_events[event] = true end
    local segments = {}
    for segment in event:gmatch('[^.]+') do
      segments[#segments + 1] = segment
    end
    recursive_resolve(sequence, event, data, waiter_registry, table.unpack(segments))
  end
end

local e = require('velvet.events').create_group('velvet.async', true)
e['**'] = resolve

--- @type table<velvet.async.event_listener, velvet.async.event_source>
local listener_to_source = make_weaktable('kv')
--- @type table<velvet.async.event_listener, velvet.async.event_source>
local source_to_listener = make_weaktable('kv')

--- listener for an event source which cannot emit new events
--- @generic T
--- @class velvet.async.event_listener<T>
--- @field wait fun(self, timeout?: nil|integer, when?: velvet.async.single_when<T>?): T wait for an event to be emitted by the source of this listener
local EventListener = {}
EventListener.__index = EventListener

function EventListener:wait(...)
  return listener_to_source[self]:wait(...)
end

--- @generic T
--- @class velvet.async.event_source<T>
--- @field emit fun(self, event?: T) emit a new event which is propagated to callers of |wait()|
--- @field wait fun(self, timeout?: nil|integer, when?: velvet.async.single_when<T>?): T wait for an event to be emitted by a call to |emit()|
--- @field listener fun(self): velvet.async.event_listener<T> returns a readonly event listener which can only access |wait()|
local EventSource = {}
EventSource.__index = EventSource

function EventSource:listener()
  if not source_to_listener[self] then
    local recv = setmetatable({}, EventListener)
    listener_to_source[recv] = self
    source_to_listener[self] = recv
  end
  return source_to_listener[self]
end

function EventSource:emit(event)
  assert(getmetatable(self) == EventSource, "Bad argument #1 (event_source expected)")
  resolve(self, event)
end

function EventSource:wait(timeout, when)
  assert(getmetatable(self) == EventSource, "Bad argument #1 (event_source expected)")
  --- @type velvet.async.event_source|velvet.async.conditional_event
  local registration = self
  if when then
    registration = {
      event = self,
      when = function(_, event) return when(event.data) end
    }
  end
  local _, result = M.wait(registration, timeout)
  return result.data
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
--- @field when? velvet.async.generic_when<any> predicate function

--- @alias velvet.async.event_registration
--- | '*'
--- | '**'
--- | string
--- | thread
--- | velvet.async.conditional_event
--- | velvet.async.event
--- | velvet.async.event_listener
--- | velvet.async.event_source

--- @class velvet.async.wait.result
--- @field event velvet.async.event|string|velvet.async.event_source the raised event
--- @field data any the event args

--- @param seq integer
--- @param co thread
--- @param ... any
local function co_resume(seq, co, ...)
  local state = co_state[co]
  if state.sequence ~= seq then return end
  state.sequence = nil
  state_cancel_timeout(state)
  sequence_callbacks[seq] = nil
  coroutine.resume(co, ...)
end

local function timeout_callback(co, timeout, seq)
  return vv.api.schedule_after(timeout, function()
    co_resume(seq, co, nil, 'timeout')
  end)
end

--- @param co thread the thread to resume when |trd| completes
--- @param trd thread the thread which |co| should wait for
--- @param seq integer sequence number identifying the wait() invocation
local function defer_callback(co, trd, seq)
  -- we must not capture |co| in this context.
  -- Otherwise this defer will pin |co| even if it is
  -- no longer possible to resume it.
  local wrap = weakref(co)
  defer_on(trd, function()
    local unwrap = wrap[1]
    if unwrap then co_resume(seq, unwrap, trd, co_state[trd].result) end
  end)
end

local function resolve_callback(co, seq)
  return function(registration, result)
    co_resume(seq, co, registration, result)
  end
end

local function is_event_source(evt)
  if type(evt) ~= 'table' then return false end
  local mt = getmetatable(evt.event or evt)
  return mt == EventListener or mt == EventSource
end

local function emit_coroutine_result(evt)
  local co = assert(currently_deferring_coroutine[1], "coroutine not alive")
  local state = co_state[co]
  evt:emit(state.result)
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
--- @param timeout? nil|integer Optional timeout
--- @return boolean success
--- @return any result
--- @return any ...
function M.wait_for_coroutine(co, timeout)
  local r, tbl = M.wait(co, timeout)
  if r == co then return table.unpack(tbl, 1, tbl['n']) else return false, 'timeout' end
end

--- Wait for one of the events to fire, or |timeout|.
--- @param ... velvet.async.event_registration|integer One or more events to wait for. A number can optionally be parsed which will be interpreted as the timeout in milliseconds.
--- @return velvet.async.event_registration, velvet.async.wait.result The argument which resolved the wait, and the wait result, or 'timeout' on timeout
function M.wait(...)
  local co = coroutine.running()
  local state = co_state[co]
  if state.deferring then error("Cannot wait() during defer.") end
  sequence = sequence + 1
  -- local capture to preserve the sequence number
  local seq = sequence
  state.sequence = seq

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
      local evt_state = co_state[evt] or
      error(string.format("bad argument %d (Provided coroutine is not managed by vv.async.)", i))
      if evt_state.result then return evt, evt_state.result end
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
    state.timeout = timeout_callback(co, timeout_value, seq)
  end

  sequence_callbacks[seq] = resolve_callback(co, seq)

  for idx, evt in ipairs(args) do
    local typename = type(evt)
    if typename == 'thread' then
      defer_callback(co, evt, seq)
    elseif is_event_source(evt) then
      local actual = listener_to_source[evt.event or evt] or evt.event or evt
      local event_waiters = event_source_waiter_registry[actual]
      event_waiters[seq] = { evt }
    elseif typename == 'string' or typename == 'table' then
      local event = evt
      if typename == 'table' then
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
  local args = table.pack(...)
  return function()
    local registration, result = M.wait(table.unpack(args, 1, args.n))
    return registration or 'timeout', result
  end
end

return M
