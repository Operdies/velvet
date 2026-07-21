--- NOTE:
--- The manual coroutine usage in these tests is not necessarily the recommended way to use this API.
--- They are being uesd this way for two reasons:
--- 1. Failed assertions inside the callbacks do not cause the test harness to fail immediately.
--- 2. The process API is not fully integrated in the async system.
--- If I get around to building an async layer around processes, the tests can be simplified,
--- but I am hesitant to build it because the async implementation must necessarily either make pessimistic
--- assumptions about usage, or

local function expect(x, y)
  assert(x == y, string.format("'%s' expected, was '%s'", x, y))
end

local function test_basic_functionality()
  local co = coroutine.running()
  local payload = [[
  printf "hello output"
  printf "hello error" >&2
]]

  local ids = {}
  local outputs = { stdout = {}, stderr = {} }
  local on_output = function(id, data, channel)
    ids[#ids + 1] = id
    local lst = outputs[channel]
    lst[#lst + 1] = data or "nil"
  end

  local proc_id = vv.api.process_spawn({ 'sh', '-c', payload }, {
    on_exit = function(id, exit_code)
      ids[#ids + 1] = id
      coroutine.resume(co, exit_code)
    end,
    on_stderr = on_output,
    on_stdout = on_output,
  })

  local exit_code = coroutine.yield()
  assert(exit_code ~= nil, "Process did not exit!")
  assert(exit_code == 0, "Process did not succeed!")
  assert(#outputs.stdout == 2)
  assert(#outputs.stderr == 2)
  assert(outputs.stdout[1] == "hello output", "Process had no stdout")
  assert(outputs.stderr[1] == "hello error", "Process had no stderr")
  assert(outputs.stdout[2] == "nil")
  assert(outputs.stderr[2] == "nil")
  assert(#ids == 5) -- twice for output, twice for closing the stream, once for exiting
  for _, id in ipairs(ids) do assert(id == proc_id, "Process id mismatch") end
end

local function test_stdin()
  do -- 1. test sending the payload on startup
    local test_input = "hello stdin"
    local co = coroutine.running()
    vv.api.process_spawn('cat', {
      on_exit = function(_, exit_code) coroutine.resume(co, exit_code) end,
      input = test_input,
      on_stdout = function(_, data, _)
        if data then coroutine.resume(co, data) end
      end,
    })
    local output = coroutine.yield()
    local exit_code = coroutine.yield()
    assert(exit_code == 0)
    assert(output == test_input)
  end

  do -- 2. test sending the payload piecemeal
    local output = ''
    local co = coroutine.running()
    local proc = vv.api.process_spawn({ 'cat' }, {
      on_exit = function(_, exit_code)
        coroutine.resume(co, exit_code)
      end,
      on_stdout = function(_, data, _)
        output = output .. (data or '')
        if data ~= nil then coroutine.resume(co) end
      end,
    })

    vv.api.process_write_stdin(proc, 'msg1\n')
    assert(output == '')
    -- wait for output to arrive
    coroutine.yield()
    assert(output == 'msg1\n')

    vv.api.process_write_stdin(proc, 'msg2\n')
    -- wait for output to arrive
    coroutine.yield()
    assert(output == 'msg1\nmsg2\n')

    -- `cat` will keep waiting until we close stdin
    vv.api.process_close_stdin(proc)

    local ok, err = pcall(vv.api.process_write_stdin, proc, 'msg3\n')
    assert(not ok)
    assert(tostring(err):match("stdin is closed"))

    local exit_code = coroutine.yield()
    assert(exit_code == 0)
    assert(output == 'msg1\nmsg2\n')
  end
end

local function test_exitcodes()
  for e = 0, 1 do
    local co = coroutine.running()
    vv.api.process_spawn({ 'sh', '-c', 'exit ' .. e }, {
      on_exit = function(_, exit_code) coroutine.resume(co, exit_code) end,
    })
    local status = coroutine.yield()
    assert(status == e)
  end
end

local function test_working_directory()
  local dirs = { '/usr/', os.getenv('HOME') }
  for _, dir in ipairs(dirs) do
    local pwd = ''
    local co = coroutine.running()
    vv.api.process_spawn({ 'pwd' }, {
      on_exit = function(_, exit_code) coroutine.resume(co, exit_code) end,
      on_stdout = function(_, data, _) if data then pwd = data:gsub("%s+", "") end end,
      working_directory = dir
    })
    local status = coroutine.yield()
    assert(status == 0, 'Expected exit code 0, was ' .. status)
    assert(pwd:gsub('/$', '') == dir:gsub('/$', ''), string.format('Expected cwd=%s, was %s', dir, pwd))
  end
end

local function test_process_kill()
  for _, delay in ipairs({ 0, 1 }) do
    local co = coroutine.running()
    local proc = vv.api.process_spawn({ 'sleep', '10' }, {
      on_exit = function(_, exit_code, signal) coroutine.resume(co, exit_code, signal) end,
    })
    if delay > 0 then
      vv.api.schedule_after(delay, function() vv.api.process_kill(proc, 'term') end)
    else
      vv.api.process_kill(proc, 'term')
    end
    local code, signal = coroutine.yield()
    assert(code == nil)
    assert(signal == 'term')
  end
end

local function test_filedescriptor_leaks()
  -- unclear why, but tests sometimes fail if I don't put a 1ms sleep here.
  vv.async.wait(1)
  local function spawn_process(id, proc_status)
    proc_status[id] = 'not yet started'
    local co = coroutine.running()
    -- ensure stdout/stderr is set and input is not set.
    -- this makes velvet allocate a pipe for each.
    vv.api.process_spawn('true', {
      on_stdout = function() end,
      on_stderr = function() end,
      on_exit = function(_, code) coroutine.resume(co, code) end,
    })
    local status = coroutine.yield()
    if status == 0 then
      proc_status[id] = 'success'
    else
      proc_status[id] = "unexpected exit code: " .. status
    end
  end

  local function spawn_procs(spawn_count)
    local proc_status = {}
    local tasklist = {}
    -- 1. spawn a ton of processes to exhaust max file descriptors
    for i = 1, spawn_count do
      proc_status[i] = 'not started'
      tasklist[i] = vv.async.run(function(id)
        local ok, err = pcall(spawn_process, id, proc_status)
        if not ok then proc_status[id] = err end
      end, i)
    end
    -- 2. wait for all tasks to complete and return the first failure, if any
    vv.async.wait_all(tasklist)
    return proc_status
  end

  local function check_first_error(spawn_result)
    for i, v in ipairs(spawn_result) do
      if v ~= 'success' then return true, i end
    end
    return false
  end

  local spawn_count = 900
  local spawn_result = spawn_procs(spawn_count)
  if check_first_error(spawn_result) ~= true then
    -- we could crank the spawn count but I don't want this test to take too long.
    WARN("fd leak test: failed to exhaust fds. Try raising the limit.")
  end

  local _, e1 = check_first_error(spawn_result)

  -- 3. now that the initial set of processes have exited,
  -- we should be able to spawn new processes again.
  -- If we did not leak any file descriptors, the errors should occur at the exact same indices.
  local second_spawn_result = spawn_procs(spawn_count)
  local _, e2 = check_first_error(second_spawn_result)
  assert(e1 == e2, "Errored at different indices! " .. e1 .. " - " .. e2)
  for i = 1, spawn_count do
    local fst = spawn_result[i]
    local snd = second_spawn_result[i]
    -- since coroutine execution is deterministic we can expect that both spawn tests yielded the same result,
    -- but only if velvet did not leak any handles
    assert(fst and fst == snd)
    if fst ~= 'success' then assert(fst:match('Error starting true:'), 'unexpected failure mode') end
  end
end

local function shell_oneline(cmd, env, debug)
  local co = coroutine.running()
  local output = nil
  vv.api.process_spawn({ 'sh', '-c', cmd }, {
    input = "",
    environment = env,
    on_stdout = function(_, data)
      if output == nil then
        output = data
        coroutine.resume(co)
      end
    end,
    on_stderr = debug and function(_, data)
      if data then WARN(data) end
    end or nil,
  })
  coroutine.yield()
  return output
end

local function test_environment()
  local env = vv.api.get_environment()
  expect('123', env.LUA_TEST_ENV)

  -- print test env set up in test harness
  local output = shell_oneline('printf $LUA_TEST_ENV')
  expect('123', output)

  -- verify the parent environment is discarded when an env table is provided
  output = shell_oneline('printf $LUA_TEST_ENV', {})
  expect(nil, output)

  -- verify inserting the existing environment table works
  output = shell_oneline('printf $LUA_TEST_ENV', env)
  expect('123', output)

  output = shell_oneline('printf $MYENV', { MYENV = "hello" })
  expect('hello', output)

  output = shell_oneline('printf "${ENV1}${ENV2}"', { ENV1 = " hello ", ENV2 = " world " })
  expect(" hello  world ", output)

  output = shell_oneline('printf "${num1}${num2}"', { num1 = 12, num2 = 34 })
  expect("1234", output)
end

local function test_spawn_errors()
  local function spawn(binary, options)
    local ok, err = pcall(vv.api.process_spawn, binary, options)
    assert(not ok)
    return tostring(err)
  end

  local bad_binary = spawn('-non-existent-binary-')
  assert(bad_binary:match("No such file or directory"))

  local bad_filetype = spawn("/etc/")
  assert(bad_filetype:match("Permission denied"))


  local bad_stdout = spawn("true", { on_stdout = true })
  assert(bad_stdout:match("function expected, got boolean"))
  local bad_stderr = spawn("true", { on_stderr = true })
  assert(bad_stderr:match("function expected, got boolean"))
  local bad_exit = spawn("true", { on_exit = {} })
  assert(bad_exit:match("function expected, got table"))
  local bad_input = spawn("true", { input = true })
  assert(bad_input:match("string expected, got boolean"))
  local bad_working_directory = spawn("true", { working_directory = {} })
  assert(bad_working_directory:match("string expected, got table"))

  local bad_environment_1 = spawn("true", { environment = true })
  assert(bad_environment_1:match("table expected, got boolean"))
  local bad_environment_2 = spawn("true", { environment = { x = function() end } })
  assert(bad_environment_2:match("environment%['x'%]: expected string, got function"), bad_environment_2)
  local bad_environment_3 = spawn("true", { environment = { [{}] = 1 } })
  assert(bad_environment_3:match("environment: expected string keys, got table"))

  local no_such_directory = spawn("true", { working_directory = "/does/not/exist" })
  assert(no_such_directory:match("No such file or directory"))
end

local function test_signal_delivery()
  local script = [[
trap 'printf int' INT
trap 'printf term' TERM
printf 'ready'
while true; do :; done
]]
  local co = coroutine.running()
  local proc = vv.api.process_spawn({ 'sh', '-c', script }, {
    on_stdout = function(_, out)
      if out then coroutine.resume(co, out) end
    end,
    on_exit = function(_, _, sig) coroutine.resume(co, sig) end,
  })

  -- ensure we don't signal the process before trap
  assert(coroutine.yield() == 'ready', 'ready')
  -- omitting the signal should send sigterm
  vv.api.process_kill(proc)
  assert(coroutine.yield() == 'term', 'expected SIGTERM')
  vv.api.process_kill(proc, 'int')
  assert(coroutine.yield() == 'int', 'expected SIGINT')
  vv.api.process_kill(proc, 'term')
  assert(coroutine.yield() == 'term', 'expected SIGTERM')
  vv.api.process_kill(proc, 'kill')
  assert(coroutine.yield() == 'kill', 'expected SIGKILL')
end

local function bug_repro()
  for i = 1, 1000 do
    local output = shell_oneline('printf "var = $MYENV"', { MYENV = "hello" .. i })
    expect('var = hello' .. i, output)
  end
end

return {
  test = function()
    test_basic_functionality()
    test_stdin()
    test_exitcodes()
    test_working_directory()
    test_process_kill()
    test_environment()
    test_environment()
    test_spawn_errors()
    test_filedescriptor_leaks()
    test_signal_delivery()
  end
}
