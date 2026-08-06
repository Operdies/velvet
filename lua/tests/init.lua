---@diagnostic disable: lowercase-global, await-in-sync

--- Module with various utilities and environmental information
--- populated by the C harness
--- @class test.utils
--- @field fatal fun(message: string): nil
--- @field stderr_isatty boolean
--- @field stdout_isatty boolean
--- @field print_timing boolean
test_utils = {}

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

function expect(x, msg)
  if x == nil or x == false then
    msg = msg or 'expect failure:'
    local traceback = debug.traceback(msg)
    local relevant = {}
    for line in traceback:gmatch("[^\r\n]+") do
      if line:match("global 'xpcall'") then break end
      relevant[#relevant + 1] = line
    end
    printerr(table.concat(relevant, '\n'))
    test_utils.fatal(traceback)
  end
  return x
end

function printerr(...)
  if test_utils.stderr_isatty then
    io.stderr:write(stringify('\x1b[31;1m', ..., '\x1b[m'))
  else
    io.stderr:write(stringify(...))
  end
end

function expect_eq(x, y)
  expect(x == y, string.format("'%s' expected, was '%s'", x, y):gsub('\n', '\\n'))
end

--- @param pattern string|number
--- @param str string|number
function expect_match(pattern, str)
  expect(str:match(pattern), string.format("Expected '%s' to match '%s'", pattern, str):gsub('\n', '\\n'))
end

function expect_error(err, fn, ...)
  local ok, result = xpcall(fn, function(e) return e end, ...)
  expect(not ok)
  if err and not result:match(err) then expect_eq(err, result) end
end

function WARN(...)
  local yellow = '\x1b[33m'
  local reset = '\x1b[m'
  local str = table.concat({ ... }, "\t")
  io.stderr:write(yellow .. str .. reset .. "\n")
end

local current_test = nil

local function run()
  local tests = {}
  -- use io.popen() for test discovery. velvet processes are equally capable and have a similar API,
  -- but io.popen() is preferred here because a velvet process bug could mask other bugs by not discovering tests.
  -- The real strength of vv.process() is the implicit async-ness via coroutines, but that flexibility is not needed here.
  io.popen('ls ../lua/tests', 'r'):lines()
  for line in io.popen('ls ../lua/tests', 'r'):lines() do
    local module = line:match('(.*).lua')
    if module and line ~= 'init.lua' then
      local load = require('tests.' .. line:match('(.*).lua'))
      local fn = load
      if type(fn) ~= 'function' then
        fn = expect(load and type(load) == 'table' and type(load.test) == 'function' and load.test,
          ("module '%s' does not export a test function."):format(module))
      end
      tests[module] = fn
    end
  end

  local failed = 0
  for name, fn in pairs(tests) do
    local start = vv.api.get_current_tick()
    current_test = vv.async.run(fn)
    local ok, err = vv.async.wait_for_coroutine(current_test)
    if test_utils.print_timing then
      print(string.format("[%4d ms] finished %s", vv.api.get_current_tick() - start, name))
    end
    current_test = nil
    if not ok then
      local lines = {}
      for line in err:gmatch("[^\r\n]+") do
        lines[#lines + 1] = line
      end
      printerr('FAIL: ' .. name .. ': ' .. lines[1])
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
    vv.async.run(function()
      local yes, why = xpcall(run, debug.traceback)
      if not yes then printerr(why) end
      TEST_STATUS = yes and why
    end)
  end
}
