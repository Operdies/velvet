---@diagnostic disable: await-in-sync
---we are accessing some vv.events() internals for testing purposes.
---@diagnostic disable: invisible
local async = vv.async
local events = vv.events

local function test_event_sources()
  local src = async.event_source()
  local resolve_tbl = {}

  local function resolve_fn(id)
    local reg, res = async.wait(src)
    resolve_tbl[id] = { reg, res }
  end

  src:emit({})

  -- set up 10 waiters
  for i = 1, 10 do
    async.run(resolve_fn, i)
  end

  assert(#resolve_tbl == 0, "Nothing should have resolved yet")

  -- emit object to waiters
  local emit = { foo = "bar" }
  src:emit(emit)

  assert(#resolve_tbl == 10)

  -- verify the same data arrived for each waiter
  for i = 1, 10 do
    assert(resolve_tbl[i][1] == src)
    assert(resolve_tbl[i][2].event == src)
    assert(resolve_tbl[i][2].data == emit)
  end

  -- verify the event source can be reused
  local emit2 = { foo = "bar" }
  local emitted = nil
  async.run(function()
    emitted = src:wait()
  end)

  assert(not emitted)
  src:emit(emit2)
  assert(emitted == emit2)

  -- verify delivery of repeated emissions
  resolve_tbl = {}
  for i = 1, 10 do
    local obj = {}
    async.run(function() resolve_tbl[i] = src:wait() end)
    src:emit(obj)
    assert(#resolve_tbl == i)
    assert(resolve_tbl[i] == obj)
    assert(resolve_tbl[i - 1] ~= obj)
  end
end

local function test_coroutine_return()
  local function test_multireturn(yield)
    local trd = async.run(function()
      -- waiting for a duration of 0 still causes the lua context to yield back to C
      -- where the main event loop will resume the test code
      if yield then async.wait(0) end
      return 123, 456
    end)
    local reg, res = async.wait(trd)
    assert(reg == trd)
    assert(res[1], "Coroutine should have succeeded")
    assert(res[2] == 123)
    assert(res[3] == 456)
    assert(not res[4])
  end

  test_multireturn(true)
  test_multireturn(false)
end

local function test_event_source_when()
  local src = async.event_source()
  local result = nil
  async.run(function()
    local _, v = async.wait({
      event = src,
      when = function(_, v)
        return v.data.x == 1
      end
    })
    result = v.data
  end)
  assert(not result)
  src:emit({ x = 2 })
  assert(not result)
  src:emit({ x = 1 })
  assert(result and result.x == 1)
end

local function test_when()
  local result = nil
  async.run(function()
    local _, v = async.wait({ event = 'my_event', when = function(_, v) return v.data.x == 1 end })
    result = v.data
  end)
  assert(not result)
  events.emit('my_event', { x = 2 })
  assert(not result)
  events.emit('my_event', { x = 1 })
  assert(result and result.x == 1)

  result = nil
  async.run(function()
    result = async.wait_for_mouse_click(nil, function(x)
      return x.event_type == 'mouse_up'
    end)
  end)
  assert(not result)
  events.emit('mouse.click', { event_type = 'mouse_down' })
  assert(not result)
  events.emit('mouse.click', { event_type = 'mouse_up' })
  assert(result)
end

local function test_wait_all()
  -- wait_all on an empty table should return immediately with an empty table
  assert(not next(async.wait_all({})))
  -- timeout should be ignored
  assert(not next(async.wait_all({}, 1000)))

  local event1 = 'custom_event_1'
  local event2 = 'custom_event_2'
  local result = nil
  local function go_await()
    result = async.wait_all({ event1, event2 }, 0)
  end

  async.run(go_await)
  assert(not result)
  async.wait(0)
  -- timeout with an empty table
  assert(result and not next(result))

  -- verify timeout returns the events that fired, and not the ones that did not
  async.run(go_await)
  local o1, o2 = { x = 1 }, { y = 2 }
  events.emit(event1, o1)
  async.wait(0)
  assert(result and result[1] and not result[2])
  assert(result[1].data.x == 1)

  -- resolve all events
  async.run(go_await)
  events.emit(event1, o1)
  events.emit(event2, o2)
  assert(result and result[1] and result[2])
  assert(result[1].data.x == 1)
  assert(result[2].data.y == 2)

  -- verify when() conditions work
  result = nil
  async.run(function()
    result = async.wait_all({
      x = { event = event1, when = function(_, evt) return evt.data.x == 1 end },
      y = { event = event2, when = function(_, evt) return evt.data.y == 2 end },
    })
  end)

  assert(not result)
  events.emit(event1, o1)
  assert(not result)
  events.emit(event2, { y = 3 })
  assert(not result)
  events.emit(event2, o2)
  assert(result)
  assert(result.x.data.x == 1)
  assert(result.y.data.y == 2)
end

local function test_event_source_wait_all()
  local s1 = async.event_source()
  local s2 = async.event_source()
  local t1 = async.run(function()
    local v1 = s1:wait()
    local v2 = s2:wait()
    return v1, v2
  end)

  local result = nil
  async.run(function()
    result = async.wait_all({ s1 = s1, s2 = s2 })
  end)

  assert(not result)
  s2:emit { 123 }
  s1:emit { 456 }
  assert(result)
  assert(result.s1.data[1] == 456)
  assert(result.s2.data[1] == 123)

  s2:emit { 789 }
  local status, v1, v2 = async.wait_for_coroutine(t1)
  assert(status and v1[1] == 456 and v2[1] == 789)

  result = nil
  async.run(function()
    local trd = async.run(function() return s1:wait(), s2:wait() end)
    result = async.wait_all({ s1 = s1, s2 = { event = s2, when = function(_, evt) return evt.data == 4 end }, t1 = trd })
  end)

  assert(not result)
  s1:emit(1) -- s1 received by wait_all and trd
  assert(not result)
  s2:emit(3) -- received by trd, rejected by wait_all
  assert(not result)
  s2:emit(4)
  assert(result)
  assert(result.s1.data == 1)
  assert(result.s2.data == 4)
  assert(result.t1.data[1] == true)
  assert(result.t1.data[2] == 1)
  assert(result.t1.data[3] == 3)

  -- verify timeout waiting for thread
  local co = async.run(function() async.wait(99999999) end)
  local cancel_result = async.wait_all({ co }, 10)
  assert(cancel_result and not next(cancel_result))

  -- verify coroutine cancelation
  cancel_result = {}
  async.run(function()
    cancel_result = async.wait_all({ co })
  end)
  assert(not cancel_result[1])
  async.cancel(co)
  assert(cancel_result[1])
  assert(cancel_result[1].data[1] == false)
  assert(cancel_result[1].data[2] == 'canceled')
end

local function test_coroutine_weakrefs()
  local threads = setmetatable({}, { __mode = 'v' })

  -- wait for every kind of supported event to
  -- verify none of the wait scenarios pin the coroutines
  local event_generators = {
    conditional_wait = function() return { event = 'some_event', when = function() return true end } end,
    named_event = function() return 'named.event' end,
    source_wait = async.event_source,
    thread_wait = function() return async.run(function() async.wait(1000) end) end,
    timeout = function() return 1000 end,
  }

  local function test_garbage_collection(name, gen)
    for i = 1, 100 do
      threads[i] = async.run(function() async.wait(gen()) end)
    end

    local function assert_liveness(liveness)
      local err = string.format(
        liveness and "wait object '%s' does not pin coroutines"
        or "wait object '%s' leaks coroutine handles", name)
      for i = 1, 100 do
        assert(liveness == (not not threads[i]), err)
      end
    end

    -- verify threads are pinned by the awaited event
    assert_liveness(true)
    collectgarbage()
    assert_liveness(true)

    -- cancel all the timers
    for i = 1, 100 do async.cancel(threads[i]) end

    -- verify collectgarbage() will collect the threads
    assert_liveness(true)
    collectgarbage()
    assert_liveness(false)
  end

  for name, gen in pairs(event_generators) do
    test_garbage_collection(name, gen)
  end

  test_garbage_collection("everything", function()
    local all = {}
    for _, gen in pairs(event_generators) do all[#all + 1] = gen() end
    return table.unpack(all)
  end)
end

local function test_delivery()
  local thread_counts = { 1, 2, 3, 5, 7 }
  for _, num_threads in ipairs(thread_counts) do
    for _, source in ipairs({ 'subject', async.event_source() }) do
      local counters = {}
      local function dummy_listener(id)
        local counter = 0
        while true do
          async.wait(source)
          counter = counter + 1
          counters[id] = counter
        end
      end
      for id = 1, num_threads do
        counters[id] = 0
        async.run(dummy_listener, id)
      end
      local function emit()
        if type(source) == 'string' then
          vv.events.emit(source)
        else
          source:emit()
        end
      end
      for i = 1, 100 do
        emit()
        for id = 1, num_threads do
          assert(counters[id] == i, "thread " .. id .. " is behind at iteration " .. i)
        end
      end
    end
  end
end

local function test_delivery_order()
  local src = async.event_source()
  -- 1000 may seem excessive, but in the past we encountered errors at 197 concurrent emitters due to a C stack overflow.
  -- This bug was fixed with a trampoline in the async emitter.
  local configurations = {
    -- one to one
    { num_emitters = 1,    num_listeners = 1 },
    -- many to one
    { num_emitters = 3,    num_listeners = 1 },
    -- one to many
    { num_emitters = 1,    num_listeners = 3 },
    -- few to many
    { num_emitters = 3,    num_listeners = 1000 },
    -- many to few
    { num_emitters = 1000, num_listeners = 3 },
  }
  for _, config in ipairs(configurations) do
    local num_emitters, num_listeners = config.num_emitters, config.num_listeners

    -- 1. set up a bunch of listeners to ensure they all see the same events in the same order
    local listeners <close> = setmetatable({}, {
      __close = function(self)
        -- cancel these threads for the next loop iteration.
        -- they don't caues problems, just slow the test down
        for _, l in ipairs(self) do
          async.cancel(l)
        end
      end
    })
    local counters = {}
    for i = 1, num_listeners do
      listeners[i] = async.run(function()
        local counter = {}
        counters[i] = counter
        while true do
          counter[#counter + 1] = src:wait()
        end
      end)
    end

    -- define an emitter which waits and emits on the same event
    for _ = 1, num_emitters do
      async.run(function()
        src:wait()
        src:emit(2)
        src:wait()
        src:emit(4)
      end)
    end

    -- first pulse
    src:emit(1)
    -- second pulse
    src:emit(3)

    -- we expect the sequence to arrive in the order:
    -- { 1, 2, 2, ..., 2, 3, 4, 4, ..., 4 }

    -- verify every listener saw the same sequence
    for _, lst in ipairs(counters) do
      -- each emitter should have emitted 2 events, plus the two pulses
      expect_eq(2 + num_emitters * 2, #lst)
      local index = 1
      expect_eq(1, lst[index]); index = index + 1 -- firsst pulse
      for _ = 1, num_emitters do
        -- each emitter should have emitted 2 once after the first pulse
        expect_eq(2, lst[index]); index = index + 1
      end
      -- second pulse
      expect_eq(3, lst[index]); index = index + 1
      -- each emitter should have emitted 4 once after the second pulse
      for _ = 1, num_emitters do
        expect_eq(4, lst[index]); index = index + 1
      end
      expect_eq(nil, lst[index]); index = index + 1
    end
  end
end

local function test_coroutine_close()
  local src = async.event_source()
  local trd = async.run(function() src:wait() end)
  coroutine.close(trd)
  local ok, err = async.wait_for_coroutine(trd)
  expect_eq(ok, false)
  expect_eq(err, 'closed')
end


return {
  test = function()
    test_when()
    test_event_sources()
    test_event_source_when()
    test_coroutine_return()
    test_wait_all()
    test_event_source_wait_all()
    test_coroutine_weakrefs()
    test_delivery()
    test_coroutine_close()
    test_delivery_order()
  end
}
