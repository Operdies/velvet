--- NOTE:
--- The manual coroutine usage in these tests is not necessarily the recommended way to use this API.
--- They are being uesd this way for two reasons:
--- 1. Failed assertions inside the callbacks do not cause the test harness to fail immediately.
--- 2. The process API is not fully integrated in the async system.
--- If I get around to building an async layer around processes, the tests can be simplified,
--- but I am hesitant to build it because the async implementation must necessarily either make pessimistic
--- assumptions about usage, or

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
  expect_eq(0, exit_code)
  expect_eq(2, #outputs.stdout)
  expect_eq(2, #outputs.stderr)
  expect_eq("hello output", outputs.stdout[1])
  expect_eq("hello error", outputs.stderr[1])
  expect_eq("nil", outputs.stdout[2])
  expect_eq("nil", outputs.stderr[2])
  expect_eq(5, #ids) -- twice for output, twice for closing the stream, once for exiting
  for _, id in ipairs(ids) do expect_eq(id, proc_id) end
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
  expect_eq('123', env.LUA_TEST_ENV)

  -- print test env set up in test harness
  local output = shell_oneline('printf $LUA_TEST_ENV')
  expect_eq('123', output)

  -- verify the parent environment is discarded when an env table is provided
  output = shell_oneline('printf $LUA_TEST_ENV', {})
  expect_eq(nil, output)

  -- verify inserting the existing environment table works
  output = shell_oneline('printf $LUA_TEST_ENV', env)
  expect_eq('123', output)

  output = shell_oneline('printf $MYENV', { MYENV = "hello" })
  expect_eq('hello', output)

  output = shell_oneline('printf "${ENV1}${ENV2}"', { ENV1 = " hello ", ENV2 = " world " })
  expect_eq(" hello  world ", output)

  output = shell_oneline('printf "${num1}${num2}"', { num1 = 12, num2 = 34 })
  expect_eq("1234", output)
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
# We need the shell to stay alive while we send signals. Each signal will interrupt a read
read arg; read arg; read arg; read arg;
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

local function test_stdout_reap_race()
  -- note that to provoke this bug originally I had to run
  -- many thousands of iterations, or put my system under extreme load,
  -- but that would make this suite extremely slow, so hopefully if the bug
  -- surfaces again this will fail intermittently.
  for i = 1, 100 do
    local output = shell_oneline('printf hello' .. i)
    expect_eq('hello' .. i, output)
  end

  -- concurrent now
  local tasks = {}
  for i = 1, 100 do
    tasks[i] = vv.async.run(shell_oneline, 'printf hello' .. i)
  end
  local results = vv.async.wait_all(tasks)
  for i = 1, 100 do
    expect_eq(true, results[i].data[1])
    expect_eq('hello' .. i, results[i].data[2])
  end
end

local function test_process_wrapper()
  local process = require('velvet.process')
  local p = process.spawn({ 'sh', '-c', 'printf "hello\nworld" ; sleep 0.1 ; printf le;' }, { stderr = false })
  expect_eq(nil, p.stderr)
  expect_eq('hello', p.stdout:line())
  expect_eq('worldle', p.stdout:line())
  expect_eq(nil, p.stdout:line())
  expect_eq(nil, p.stdout:line())
  expect_eq(nil, p.stdout:lines()())
  expect_eq(0, p:wait_for_exit())

  p = process.spawn({ 'sh', '-c', 'printf "hello\nworld\n"' })
  local lines = {}
  for line in p.stdout:lines() do lines[#lines+1] = line end
  assert(2, #lines)
  expect_eq('hello', lines[1])
  expect_eq('world', lines[2])
  expect_eq(0, p:wait_for_exit())

  local payload = [[ i=1
while [ "$i" -le 100 ]; do
  echo "$i"
  i=$((i+1))
done ]]
  p = process.spawn({ 'sh', '-c', payload }, { stdin = false, stderr = false })
  for i = 1, 100 do
    expect_eq(tostring(i), p.stdout:line())
  end
  expect_eq(nil, p.stdout:line())

  payload = [[ while read MY_ARG; do
  echo "$MY_ARG"
done ]]
  p = process.spawn({ 'sh', '-c', payload }, { stdin = true, stderr = false })
  local items = { "this", "is", "my", "list", "of", "awesome and cool", "strings" }
  for _, v in ipairs(items) do
    p.stdin:write(v .. '\n')
    expect_eq(v, p.stdout:line())
  end
  local large_string = string.rep('what a strange string this is', 1000)
  for _ = 1, 10 do
    p.stdin:write(large_string .. '\n')
    expect_eq(large_string, p.stdout:line())
  end
  p.stdin:write(large_string .. '\n')
  p.stdin:close()
  expect_eq(large_string .. '\n', p.stdout:read_all())
  expect_eq(nil, p.stdout:line())

  p = process.spawn({ 'sh', '-c', 'printf "hello\nworld" ; printf "le";' }, { stderr = false })
  expect_eq('hello\nworldle', p.stdout:read_all())
  expect_eq(nil, p.stdout:read_all())
  expect_eq(0, p:wait_for_exit())

  p = process.spawn({ 'sh', '-c', 'printf "hello\nworld" ; printf "le\n";' }, { stderr = false })
  expect_eq('hello\nworldle\n', p.stdout:read_all())
  expect_eq(nil, p.stdout:read_all())
  expect_eq(0, p:wait_for_exit())


  -- test stderr stream
  payload = [[ printf "hello output"
  printf "hello error" >&2 ]]
  p = process.spawn({ 'sh', '-c', payload })
  expect_eq('hello error', p.stderr:line())
  expect_eq(nil, p.stderr:line())
  expect_eq('hello output', p.stdout:line())
  expect_eq(nil, p.stdout:line())

  -- ensure repeated line() calls return nil
  expect_eq(nil, p.stderr:line())
  expect_eq(nil, p.stdout:line())

  -- ensure read_all() returns nil
  expect_eq(nil, p.stderr:read_all())
  expect_eq(nil, p.stdout:read_all())

  -- lines() should not have any elements after consuming the stream
  for line in p.stderr:lines() do
    assert(false, "should be no lines! " .. line)
  end

  for line in p.stdout:lines() do
    assert(false, "should be no lines! " .. line)
  end
  expect_eq(0, p:wait_for_exit())

  -- test iterators with interleaved stdout/stderr output
  payload = [[ printf "hello output 1\n"
  printf "hello error 1\n" >&2
  printf "hello output 2\n"
  printf "hello output 3\n"
  printf "hello error 2\n" >&2
  printf "hello error 3\n" >&2
  printf "hello error 4\n" >&2
  printf "hello output 4\n"
  printf "hello output 5\n" ]]
  p = process.spawn({ 'sh', '-c', payload })
  local counter = 1
  for line in p.stderr:lines() do
    expect_eq('hello error ' .. counter, line)
    counter = counter + 1
  end
  counter = 1
  for line in p.stdout:lines() do
    expect_eq('hello output ' .. counter, line)
    counter = counter + 1
  end

  -- test read_all
  payload = [[ printf "hello output 1"
  printf "hello error 1\n" >&2
  printf "hello output 2\n" ]]
  p = process.spawn({ 'sh', '-c', payload })
  expect_eq("hello output 1" .. "hello output 2\n", p.stdout:read_all())
  expect_eq("hello error 1\n", p.stderr:read_all())
end

return {
  test = function()
    -- it is kind of necessary to insert sleeps in some process related
    -- tests because buffering behavior is timing sensitive by nature.
    -- we run these tests in parallel to save a bit of time.
    -- Parallel processes is closer to a real world scenario anyway.
    local tests = {
      test_basic_functionality,
      test_environment,
      test_exitcodes,
      test_process_kill,
      test_process_wrapper,
      test_signal_delivery,
      test_spawn_errors,
      test_stdin,
      test_stdout_reap_race,
      test_working_directory,
    }

    local tasks = {}
    for i, v in ipairs(tests) do
      tasks[i] = vv.async.run(v)
    end
    local result = vv.async.wait_all(tasks)
    for _, v in ipairs(result) do
      local ret = v.data
      assert(ret[1], ret[2])
    end
    -- we can't run this alongside the other tests because it is designed to cause errors
    test_filedescriptor_leaks()
  end
}
