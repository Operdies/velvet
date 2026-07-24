-- globals overriden by the test harness
SIGTERM = 0
STDERR_ISATTY = false
STDOUT_ISATTY = false

local tests = {
  'tests.test_process',
  'tests.test_deep_extend',
  'tests.test_runtime_storage',
  'tests.test_async'
}

-- vv redefines print() to use vv.log,
-- so locally we should route print output to io.write() instead.
-- under normal operation, io.write() is not supported because
-- velvet doesn't really define where the STDOUT / STDERR file descriptors
-- are pointing, but this will only be used in a unit-testing context.
function print(...)
  local str = table.concat({ ... }, "\t")
  io.write(str .. "\n")
end

local function printerr(...)
  local str = table.concat({ ... }, "\t")
  if STDERR_ISATTY then
    -- bold red
    str = '\x1b[31;1m' .. str .. '\x1b[m'
  end
  io.stderr:write(str .. '\n')
end

function WARN(...)
  local yellow = '\x1b[33m'
  local reset = '\x1b[m'
  local str = table.concat({ ... }, "\t")
  io.stderr:write(yellow .. str .. reset .. "\n")
end

local function run()
  local failed = 0
  local tasks = {}
  for _, mod in ipairs(tests) do
    local test = require(mod).test
    tasks[mod] = vv.async.run(test)
  end
  local results = vv.async.wait_all(tasks)
  for mod, result in pairs(results) do
    local ret = result.data
    local ok = ret[1]
    if not ok then
      local err = ret[2]
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
end

return {
  run = function()
    vv.api.schedule_after(5000, function() TEST_STATUS = false end)
    vv.async.run(function()
      local status, result = pcall(run)
      if not status then print(result) end
      TEST_STATUS = status
    end)
  end
}
