local async = vv.async
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
    assert(resolve_tbl[i-1] ~= obj)
  end
end

local function test_coroutine_return()
  local function test_multireturn(yield)
    local trd = async.run(function()
      -- waiting for a duration of 0 still causes the lua context to yield back to C 
      -- where the main event loop will resume the test code
      if yield then async.wait(0) end
      return 123, 456
    end
    )
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
  vv.async.run(function()
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
  vv.async.run(function()
    local _, v = async.wait({ event = 'my_event', when = function(_, v) return v.data.x == 1 end })
    result = v.data
  end)
  assert(not result)
  vv.events.emit('my_event', { x = 2 })
  assert(not result)
  vv.events.emit('my_event', { x = 1 })
  assert(result and result.x == 1)
end

return {
  test = function()
    test_when()
    test_event_sources()
    test_event_source_when()
    test_coroutine_return()
  end
}
