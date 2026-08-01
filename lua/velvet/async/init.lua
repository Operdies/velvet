--- The async API is a coroutine based implementation of velvet's event system, enabling linear control flow.

local wait_functions = require('velvet.async.wait_functions')
local known_events = wait_functions[1]
local M = wait_functions[2]

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

--- @param fn function(): nil
--- @return table
local function make_close(fn, arg)
  return setmetatable({}, {
    __close = function()
      local ok, err = xpcall(fn, debug.traceback, arg)
      if not ok then printerr("Unhandled error during close: " .. err) end
    end
  })
end

--- @generic T any
--- @param obj T object to wrap
--- @return [T] wrap array containing a weak reference to |obj|
local function weakref(obj) return setmetatable({ obj }, { __mode = 'v' }) end

--- @alias velvet.async.resolve fun(reg: velvet.async.event_registration, result: velvet.async.wait.result)
--- @alias velvet.async.resolve_table table<integer, velvet.async.resolve>

--- @generic T
--- @alias velvet.async.single_when<T> fun(data: T): boolean

--- @generic T
--- @alias velvet.async.generic_when<T> fun(registration: velvet.async.event_registration, event: velvet.async.wait.result): boolean

--- @class velvet.async.waiter_registrations
--- @field [integer] velvet.async.event_registration[]

--- mapping from an integer handle to a resolve callback.
--- @type velvet.async.resolve_table
local sequence_callbacks = {}

--- @type table<string, velvet.async.waiter_registrations>
local waiter_registry = {}

-- NOTE: this registry is weakly key'd because it is safe to gc
-- registrations if the source object is no longer reachable.
-- This is not the case for waiter_registry, because a string may still be composed
-- even if it is not reachable at this point in time.
--- @type table<velvet.async.event_source, velvet.async.waiter_registrations>
local event_source_waiter_registry = make_weaktable('k')

--- @class velvet.async.coroutine_state
--- @field deferring? boolean true if the coroutine is currently deferring
--- @field result? any[]? set if the coroutine is completed or canceled
--- @field sequence? integer lookup key to |sequence_callbacks|
--- @field defers any[] list of deferred functions and their parameters
--- @field timeout? integer timeout token for this coroutine

--- @type table<thread, velvet.async.coroutine_state>
local co_state = make_weaktable('k')

--- @class velvet.async.event_source_waiters
--- @field [integer] velvet.async.event_registration[]

--- weak-valued table containing the currently deferring coroutine, or nil
--- @type [thread?]
local currently_deferring_coroutine = make_weaktable('v')

-- Monotonically increasing sequence number used to invalidate stale waiters
local sequence_counter = 0
local function next_sequence()
  sequence_counter = sequence_counter + 1
  return sequence_counter
end

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
  assert(state.defers, "thread deferred twice!")
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

  local function defer()
    -- the thread may have already deferred if it was cancelled.
    -- if so, its defer table is nil
    if state.defers ~= nil then
      -- if xpcall did not return, then the coroutine must have been closed via coroutine.close().
      if not state.result then
        state.result = { false, 'closed' }
      end
      exec_defer(co)
    end
  end

  local defer_handle <close> = make_close(defer)
  state.result = table.pack(xpcall(f, debug.traceback, ...))
end

--- Execute |f| in a coroutine with the given arguments.
--- @param f async fun(...): ...
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

--- @param wait_table velvet.async.waiter_registrations
--- @param event string|velvet.async.event_source
--- @param data velvet.async.wait.result
local function resolve_table(wait_table, event, data)
  -- Before resolving any events, record the current sequences in the wait table. This achieves 2 things:
  -- 1. Guards against iteration errors due to the table being modified by the call to waiter().
  -- This is a quite common because a listener will often call wait() again on the same event.
  -- 2. Ensures we only resolve events which were registered before the event was emitted.
  -- Otherwise new registrations could be resolved with an event which was emitted before it was created.
  local sequences = {}
  for sequence in pairs(wait_table) do
    if type(sequence) == 'number' then
      if sequence_callbacks[sequence] then
        sequences[#sequences + 1] = sequence
      else
        -- if this sequence number does not have an associated callback,
        -- the context has already been resolved or cancelled.
        -- In that case, we can safely clear it from this table.
        wait_table[sequence] = nil
      end
    end
  end

  for i = 1, #sequences do
    local sequence = sequences[i]
    -- although sequence_callbacks[sequence] was checked in the previous loop,
    -- it can still have been nil'ed in a recursive event dispatch in the below call to waiter()
    local callback = sequence_callbacks[sequence]
    if not callback then goto next_sequence end
    local registrations = wait_table[sequence]
    if not registrations then goto next_sequence end
    for j = 1, #registrations do
      local reg = registrations[j]
      local is_match = true
      local wait_result = { event = event, data = data }
      if reg.when then
        local ok, result = xpcall(reg.when, debug.traceback, reg, wait_result)
        if not ok then
          local event_name = type(event) == 'string' and event or 'event_source'
          printerr(string.format("Unhandled error during when(%s): %s", event_name, result))
          is_match = false
        else
          is_match = result
        end
      end
      if is_match then
        wait_table[sequence] = nil
        callback(reg, wait_result)
        break
      end
    end
    ::next_sequence::
  end
end

--- @class velvet.async.result_lock
--- @field queue { [integer]: thread }
--- @field resolving boolean set if currently resolving
--- @field schedule integer|nil event loop token
local resolve_lock = { queue = {}, resolving = false, schedule = nil }

function resolve_lock.pop_frame()
  local function pop()
    -- resume all threads which were yielded while this frame was being dispatched
    local queue = resolve_lock.queue
    resolve_lock.queue = {}
    for i = 1, #queue do
      coroutine.resume(queue[i])
    end
  end

  pop()
  resolve_lock.resolving = false

  -- if popping the resolve stack queued new resolve frames,
  -- they will be dispatched the next time an event is emitted.
  -- However, if this does not happen before the context returns to C,
  -- this will not happen. To work around this, we schedule an immediate flush on the event loop.
  if resolve_lock.schedule == nil and resolve_lock.queue[1] then
    resolve_lock.schedule = vv.api.schedule_after(0, function()
      resolve_lock.schedule = nil
      if resolve_lock.queue[1] then
        resolve_lock.resolving = true
        resolve_lock.pop_frame()
      end
    end)
  end
end


resolve_lock.pop = make_close(resolve_lock.pop_frame)

function resolve_lock.push_frame()
  if resolve_lock.resolving then
    -- if another event is currently being resolved, yield.
    -- this thread will be resumed after the currently resolving event is finished.
    local idx = #resolve_lock.queue + 1
    resolve_lock.queue[idx] = coroutine.running()
    -- yield will fail if it would attempt to yield across a C-call boundary.
    -- In this case, we should just emit the event but skip the trampoline.
    if not pcall(coroutine.yield) then resolve_lock.queue[idx] = nil end
    return nil
  end

  resolve_lock.resolving = true
  return resolve_lock.pop
end


--- @param event string|velvet.async.event_source
--- @param data velvet.async.wait.result
local function resolve(event, data)
  -- if a waiter emits a new event while this event is resolving,
  -- we want to finish resolving the current event before resolving the next one.
  -- resolve_lock accomplishes this by yielding the emitter if necessary and resuming it
  -- once the initiating event has been fully resolve.
  --
  -- In particular, this fixes an issue where a thread waiting for a single event in a loop
  -- will drop instances of the event, or even receive them in different orders, if another thread
  -- emits the event after waiting for it.
  --
  -- See the test in |tests.async.test_delivery_order| for a concrete reproduction.
  -- push() returns a metatable with __close defined so the frame will be popped when this scope closes.
  -- We need to use <close> here because we would otherwise miss the case where this coroutine is killed via coroutine.close()
  local frame <close> = resolve_lock.push_frame()

  if type(event) == 'table' then
    local waiters = event_source_waiter_registry[event]
    if waiters then resolve_table(waiters, event, data) end
  elseif type(event) == 'string' then
    local waiters = waiter_registry[event]
    if waiters then resolve_table(waiters, event, data) end
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
--- @field wait async fun(self, timeout?: nil|integer, when?: velvet.async.single_when<T>?): T wait for an event to be emitted by the source of this listener
local EventListener = {}
EventListener.__index = EventListener

--- @async always yields
function EventListener:wait(...)
  return listener_to_source[self]:wait(...)
end

--- @generic T
--- @class velvet.async.event_source<T>
--- @field emit fun(self, event?: T) emit a new event which is propagated to callers of |wait()|
--- @field wait async fun(self, timeout?: nil|integer, when?: velvet.async.single_when<T>?): T wait for an event to be emitted by a call to |emit()|
--- @field listener fun(self): velvet.async.event_listener<T> returns a readonly event listener which can only access |wait()|
--- @field closed? boolean set when the event source is closed
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

--- @async always yields
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
  assert(type(timeout) == 'nil' or math.type(timeout) == 'integer',
    string.format("Bad argument #2 (integer expected, got %s)", type(timeout)))
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

--- @param sequence integer
--- @param co thread
--- @param ... any
local function co_resume(sequence, co, ...)
  local state = co_state[co]
  if state.sequence ~= sequence then return end
  state.sequence = nil
  state_cancel_timeout(state)
  sequence_callbacks[sequence] = nil
  coroutine.resume(co, ...)
end

local function timeout_callback(co, timeout, sequence)
  return vv.api.schedule_after(timeout, function()
    co_resume(sequence, co, nil, 'timeout')
  end)
end

--- @param co thread the thread to resume when |trd| completes
--- @param trd thread the thread which |co| should wait for
--- @param sequence integer sequence number identifying the wait() invocation
local function defer_callback(co, trd, sequence)
  -- we must not capture |co| in this context.
  -- Otherwise this defer will pin |co| even if it is
  -- no longer possible to resume it.
  local wrap = weakref(co)
  defer_on(trd, function()
    local unwrap = wrap[1]
    if unwrap then co_resume(sequence, unwrap, trd, co_state[trd].result) end
  end)
end

local function resolve_callback(co, sequence)
  return function(registration, result)
    co_resume(sequence, co, registration, result)
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
--- @async yields if |events| contains a valid event.
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
      local state = co_state[reg]
      if not state.deferring then
        defer_on(reg, emit_coroutine_result, event)
      else
        -- it is not legal to schedule defers on an already deferring coroutine,
        -- so instead we schedule emitting the coroutine result immediately
        vv.api.schedule_after(0, function()
          event:emit(state.result)
        end)
      end
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

--- Wait for |co| to complete. Its first result is the status code (a boolean), which is true if the coroutine completed without errors. In such case, `wait_for_coroutine` also returns all results from the function, after this first result. In case of any error, `wait_for_coroutine` returns `false` plus the error object.
--- @async yields if |co| has not yet completed
--- @param co thread
--- @param timeout? nil|integer Optional timeout
--- @return boolean success
--- @return any result
--- @return any ...
function M.wait_for_coroutine(co, timeout)
  local r, tbl = M.wait(co, timeout)
  if r == co then return table.unpack(tbl, 1, tbl['n']) else return false, 'timeout' end
end

--- Yield the current thread. This is useful for giving control back to the system during a heavy computation,
--- or in certain scenarios where the thread might want to give control back to its parent.
--- @async always yields
function M.yield()
  -- A wait of 0ms will allow the lua context to return back to C where velvet can complete any pending tasks, such as process io.
  -- This thread will be resumed immediately after there is no more work to do.
  vv.async.wait(0)
end

--- Wait for one of the events to fire, or |timeout|.
--- @async yields if any valid events are provided.
--- @param ... velvet.async.event_registration|integer One or more events to wait for. A number can optionally be parsed which will be interpreted as the timeout in milliseconds.
--- @return velvet.async.event_registration, velvet.async.wait.result The argument which resolved the wait, and the wait result, or 'timeout' on timeout
function M.wait(...)
  local co = coroutine.running()
  local state = co_state[co]
  if not state then
    error("Calling thread is not managed by vv.async")
  end
  if state.deferring then error("Cannot wait() during defer.") end
  local sequence = next_sequence()
  state.sequence = sequence

  local compatible_types = { number = true, string = true, table = true, thread = true }
  local raw_args = table.pack(...)
  local timeout_value = nil
  local args = {}
  -- for i, evt in ipairs(raw_args) do
  for i = 1, raw_args.n do
    local evt = raw_args[i]
    if evt ~= nil then
      local tp = type(evt)
      if not compatible_types[tp] then
        error(("Bad argument #%d (number, string, coroutine, or table expected)"):format(i))
      end
      if tp == 'thread' then
        -- if the coroutine completed, or is not running, return immediately.
        local evt_state = co_state[evt] or
            error(string.format("Bad argument %d (Provided coroutine is not managed by vv.async.)", i))
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
  end

  if timeout_value then
    state.timeout = timeout_callback(co, timeout_value, sequence)
  end

  sequence_callbacks[sequence] = resolve_callback(co, sequence)

  for idx = 1, #args do
    local evt = args[idx]
    local typename = type(evt)
    if typename == 'thread' then
      defer_callback(co, evt, sequence)
    elseif is_event_source(evt) then
      local actual = listener_to_source[evt.event or evt] or evt.event or evt
      local wait_table = event_source_waiter_registry[actual]
      if wait_table[sequence] then
        table.insert(wait_table[sequence], evt)
      else
        wait_table[sequence] = { evt }
      end
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
      local wait_table = waiter_registry[event]
      if wait_table == nil then
        wait_table = {}
        waiter_registry[event] = wait_table
      end
      if wait_table[sequence] then
        -- this event has multiple registrations on the same event.
        -- there is nothing wrong with this since the registrations can have
        -- different |when| triggers, but we need to handle this by chaining
        -- the registrations.
        table.insert(wait_table[sequence], evt)
      else
        wait_table[sequence] = { evt }
      end
    else
      error(('Bad argument #%d (string|number expected, got %s)'):format(idx, type(evt)))
    end
  end

  return coroutine.yield()
end

--- Returns an iterator which yields whenever an event in |...| is fired. Terminates on timeout if specified.
--- @param ... velvet.async.event_registration|integer One or more events to stream. A number can optionally be parsed which will be interpreted as the timeout in milliseconds.
--- @return async fun(): velvet.async.event_registration, velvet.async.wait.result Iterator which streams the input events
function M.stream(...)
  local args = table.pack(...)
  --- @async
  return function()
    local registration, result = M.wait(table.unpack(args, 1, args.n))
    return registration or 'timeout', result
  end
end

return M
