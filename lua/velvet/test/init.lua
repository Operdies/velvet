local tests = {
  'velvet.test.test_deep_extend',
  'velvet.test.test_runtime_storage',
  'velvet.test.test_async'
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

local function run()
  local failed = 0
  for _, mod in ipairs(tests) do
    local test = require(mod).test
    local ok, err = xpcall(test, debug.traceback)
    if not ok then
      local lines = {}
      for line in err:gmatch("[^\r\n]+") do
        lines[#lines+1] = line
      end
      print('FAIL: ' .. mod .. ': ' .. lines[1])
      for i = 2,#lines do
        local line = lines[i]
        if line:match("in global 'xpcall'") then break end
        print(line)
      end
      failed = failed + 1
    end
  end
  if failed > 0 then
    error(failed .. ' test module(s) failed')
  end
end

return {
  run = function()
    vv.async.run(function()
      local status, result = pcall(run)
      if not status then print(result) end
      TEST_STATUS = status
    end)
  end
}
