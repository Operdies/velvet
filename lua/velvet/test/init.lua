local tests = {
  'velvet.test.test_deep_extend',
  'velvet.test.test_session_storage',
}

local function run()
  local failed = 0
  for _, mod in ipairs(tests) do
    print('running: ' .. mod)
    local ok, err = pcall(require, mod)
    if not ok then
      print('FAIL: ' .. mod .. ': ' .. tostring(err))
      failed = failed + 1
    end
  end
  if failed > 0 then
    error(failed .. ' test module(s) failed')
  end
  print('all ' .. #tests .. ' test module(s) passed')
end

return { run = run }
