---@diagnostic disable: lowercase-global, await-in-sync
-- globals overriden by the test harness
SIGTERM = 0
STDERR_ISATTY = false
STDOUT_ISATTY = false

local tests = {
  'tests.test_process',
  'tests.test_deep_extend',
  'tests.test_runtime_storage',
  'tests.test_async',
  'tests.test_grid',
  'tests.test_cli',
}

local function stringify(...)
  local tbl = table.pack(...)
  for i = 1, tbl.n do
    tbl[i] = tostring(tbl[i])
  end
  return table.concat(tbl, "\t") .. '\n'
end

-- vv redefines print() to use vv.log,
-- so locally we should route print output to io.write() instead.
-- under normal operation, io.write() is not supported because
-- velvet doesn't really define where the STDOUT / STDERR file descriptors
-- are pointing, but this will only be used in a unit-testing context.
function print(...)
  io.write(stringify(...))
end

function printerr(...)
  if STDERR_ISATTY then
    io.stderr:write(stringify('\x1b[31;1m', ..., '\x1b[m'))
  else
    io.stderr:write(stringify(...))
  end
end

function expect_eq(x, y)
  assert(x == y, string.format("'%s' expected, was '%s'", x, y):gsub('\n', '\\n'))
end

--- @param pattern string|number
--- @param str string|number
function expect_match(pattern, str)
  assert(str:match(pattern), string.format("Expected '%s' to match '%s'", pattern, str):gsub('\n', '\\n'))
end

function expect_error(err, fn, ...)
  local ok, result = xpcall(fn, function(e) return e end, ...)
  assert(not ok)
  if err and not result:match(err) then expect_eq(err, result) end
end

function WARN(...)
  local yellow = '\x1b[33m'
  local reset = '\x1b[m'
  local str = table.concat({ ... }, "\t")
  io.stderr:write(yellow .. str .. reset .. "\n")
end

local current_test = nil

local print_timing = false

local function run()
  local failed = 0
  for _, mod in ipairs(tests) do
    local test = require(mod).test
    current_test = vv.async.run(test)
    local start = vv.api.get_current_tick()
    local ok, err = vv.async.wait_for_coroutine(current_test)
    if print_timing then
      local test_name = mod:match("[^.]+$")
      print(string.format("[%4d ms] finished %s", vv.api.get_current_tick() - start, test_name))
    end
    current_test = nil
    if not ok then
      local lines = {}
      for line in err:gmatch("[^\r\n]+") do
        lines[#lines + 1] = line
      end
      printerr('FAIL: ' .. mod .. ': ' .. lines[1])
      for i = 2, #lines do
        local line = lines[i]
        if line:match("in global 'xpcall'") then break end
        printerr(line)
      end
      failed = failed + 1
    end
  end
  if failed > 0 then
    printerr(failed .. ' test module(s) failed')
  end
  return failed == 0
end

return {
  run = function()
    local trd = vv.async.run(function()
      local status, result = pcall(run)
      if not status then print(result) end
      TEST_STATUS = status and result
    end)

    -- timing out misbehaving tests is unreliable because coroutines are in the end still cooperative.
    -- if a thread is doing a lot of work without yielding, this action will be delayed until it is done.
    -- but having a timeout is better than no timeout. The better soluton would be for the C harness
    -- to install a debug break and dump the VM state if it exceeds some instruction limit.
    vv.api.schedule_after(5000, function()
      TEST_STATUS = false
      vv.async.cancel(trd)
      if current_test ~= nil then vv.async.cancel(current_test) end
    end)
  end
}
