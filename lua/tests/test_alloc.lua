local function measure(fn, ...)
  local now = os.clock()
  local start = vv.api.get_current_tick()
  fn(...)
  -- include garbage collection in this benchmark.
  -- otherwise unrelated tests may be penalized by this test's allocations.
  -- and this test should pay for its allocations.
  collectgarbage()
  local tick_time = vv.api.get_current_tick() - start
  return os.clock() - now, tick_time
end

local function alloc_tables(cnt)
  for _ = 1, cnt do
    local _ = {}
  end
end

local function alloc_closures(cnt)
  for _ = 1, cnt do (function() end)() end
end

local function xpcall_closures(cnt)
  local fn = function() return 1, 2 end
  for _ = 1, cnt do xpcall(fn, debug.traceback) end
end

local function pack_xpcall_closures(cnt)
  local fn = function() end
  for _ = 1, cnt do local _ = table.pack(xpcall(fn, debug.traceback)) end
end

local function wrap_xpcall_closures(cnt)
  local fn = function() end
  for _ = 1, cnt do local _ = { xpcall(fn, debug.traceback) } end
end

local function alloc_yielding_threads(cnt)
  local noop = function(x) return coroutine.yield(x) end
  for _ = 1, cnt do
    local co = coroutine.create(noop)
    coroutine.resume(co, noop, 1)
    coroutine.resume(co, 2)
  end
end

local function alloc_threads(cnt)
  local noop = function() end
  for _ = 1, cnt do
    local co = coroutine.create(noop)
    coroutine.resume(co, noop)
  end
end

local function async_run(cnt)
  local r = vv.async.run
  local noop = function() end
  for _ = 1, cnt do
    r(noop)
  end
end


return {
  test = function()
    local n_tables = 1e7
    local n_threads = 1e6

    local function apply(fn, x)
      return function() return fn(x) end
    end

    local benches = {
      { name = "alloc closures",       measure = apply(alloc_closures, n_tables) },
      { name = "xpcall closures",      measure = apply(xpcall_closures, n_tables) },
      { name = "pack xpcall closures", measure = apply(pack_xpcall_closures, n_tables) },
      { name = "wrap xpcall closures", measure = apply(wrap_xpcall_closures, n_tables) },
      { name = "alloc tables",         measure = apply(alloc_tables, n_tables) },
      { name = "co resume",            measure = apply(alloc_threads, n_threads) },
      { name = "co resume yield",      measure = apply(alloc_yielding_threads, n_threads) },
      { name = "vv.async.run",         measure = apply(async_run, n_threads) },
    }

    for _, bench in ipairs(benches) do
      local duration, ms = measure(bench.measure)
      print(string.format("bench: %-20s in %f s (%d ms)", bench.name, duration, ms))
    end
  end
}
