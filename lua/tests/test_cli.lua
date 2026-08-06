---@diagnostic disable: await-in-sync
-- setup {{{1
-- find the vv binary next to the test binary
local vv_binary = vv.cwd() .. '/vv'
local servername = 'test-server'

local process = require('velvet.process')

local env = vv.api.get_environment()

local server = nil

local function start_server(socket)
  -- spawn a new server with a minimal environment
  local server_env = { HOME = env.HOME }
  -- spawn the server as a non-daemonizing (foreground) process with a --clean config, meaning it uses exactly the default lua config.
  -- pass some additional arguments to later test that they are correctly added to `vv.startup_arguments`
  server = process.spawn(
    { vv_binary, 'foreground', '--clean', '--socket', socket, "--", "first arg", "second arg" },
    { environment = server_env })
  -- Wait for the server to listen before proceeding. The read timeout is set very high
  -- because macOS can be extremely slow when starting a process for the first time.
  expect_eq("Server listening at " .. socket, server.stdout:line(5000))
  -- kill the server if this test fails before the shutdown test
  vv.async.defer(function()
    if server ~= nil then
      server:kill('term')
    end
  end)
end

-- convenience function for running a lua chunk on the test server
local function run_lua(lua, expected_exitcode, ...)
  local p = process.spawn({ vv_binary, '--socket', servername, 'lua', ... })
  p.stdin:write(lua)
  p.stdin:close()
  local exit = p:wait_for_exit(100)
  assert(exit ~= nil, "timeout waiting for exit")
  local stdout = p.stdout:read_all()
  local stderr = p.stderr:read_all()
  if expected_exitcode == nil or expected_exitcode == 0 then
    expect_eq(nil, stderr)
  end
  expect_eq(expected_exitcode or 0, exit)
  return stdout, stderr
end

local function expect_lua(str, lua, expected_exitcode)
  expect_eq(str, run_lua(lua, expected_exitcode))
end

local function run_inspect(lua, ...)
  local payload = 'print(vv.inspect(' .. lua .. '))'
  local response = run_lua(payload, 0, ...)
  local obj, err = load('return ' .. response)
  assert(obj, err)
  return obj()
end

local function shell_load(cmd)
  local proc = process.spawn({ 'bash', '-c', '$VV lua ' .. cmd }, {
    environment = {
      -- to simplify cmd invocations, set test environment variables
      VELVET = servername,
      VV = vv_binary,
      HOME = env.HOME,
    }
  })
  local exit = proc:wait_for_exit(100)
  assert(exit ~= nil, "timeout waiting for exit")
  expect_eq(nil, proc.stderr:read_all())
  expect_eq(0, exit)
  local out = proc.stdout:read_all()
  local obj, err = load('return ' .. out)
  assert(obj, err)
  return obj()
end

local function test_server_cli() -- {{{1
  -- test server args
  expect_lua("123\n", 'print(123)')
  expect_lua("first arg\n", 'return vv.startup_arguments[1]')
  expect_lua("second arg\n", 'return vv.startup_arguments[2]')
  expect_lua(nil, 'return vv.startup_arguments[3]')

  -- test cli args
  local args = run_inspect('arg')
  expect_eq('<stdin>', args[0])
  expect_eq(nil, args[1])

  args = run_inspect('arg', '-', 'test one', 1)
  expect_eq('<stdin>', args[0])
  expect_eq('test one', args[1])
  expect_eq('1', args[2])
  expect_eq(nil, args[3])

  args = run_inspect('arg', '--', 'test two', 2)
  expect_eq('<stdin>', args[0])
  expect_eq('test two', args[1])
  expect_eq('2', args[2])
  expect_eq(nil, args[3])

  local shell_tempfile = [[<(echo 'return vv.inspect(arg)')]]
  local stdin_redirect = [[< <(echo 'return vv.inspect(arg)')]]

  -- read stdin from temporary file (shell created, usually /proc/self/fd/%d
  args = shell_load(string.format([[%s 'test three' 3]], shell_tempfile))
  expect_match('/fd/', args[0])
  expect_eq('test three', args[1])
  expect_eq('3', args[2])
  expect_eq(nil, args[3])

  -- read stdin from stdin, but redirected from a file
  args = shell_load(string.format([[%s -- 'test four' 4]], stdin_redirect))
  expect_eq('<stdin>', args[0])
  expect_eq('test four', args[1])
  expect_eq('4', args[2])
  expect_eq(nil, args[3])

  -- stdin redirect, but as the last argument
  args = shell_load(string.format([[-- "test five" 5 %s]], stdin_redirect))
  expect_eq('<stdin>', args[0])
  expect_eq('test five', args[1])
  expect_eq('5', args[2])
  expect_eq(nil, args[3])

  local inspect = [['return vv.inspect(arg)']]

  -- herestring stdin
  args = shell_load([[-- "test six" 6 <<< ]] .. inspect)
  expect_eq('<stdin>', args[0])
  expect_eq('test six', args[1])
  expect_eq('6', args[2])
  expect_eq(nil, args[3])

  -- herestring stdin 2
  args = shell_load([[- "test seven" 7 <<< ]] .. inspect)
  expect_eq('<stdin>', args[0])
  expect_eq('test seven', args[1])
  expect_eq('7', args[2])
  expect_eq(nil, args[3])

  -- no args, herestring 1
  args = shell_load([[<<< 'return vv.inspect{"test eight", 8}']])
  expect_eq(nil, args[0])
  expect_eq('test eight', args[1])
  expect_eq(8, args[2])
  expect_eq(nil, args[3])

  -- no args, herestring 2
  args = shell_load([[- <<< 'return vv.inspect{"test nine", 9}']])
  expect_eq(nil, args[0])
  expect_eq('test nine', args[1])
  expect_eq(9, args[2])
  expect_eq(nil, args[3])


  -- no args, herestring 3
  args = shell_load([[-- <<< 'return vv.inspect{"test ten", 10}']])
  expect_eq(nil, args[0])
  expect_eq('test ten', args[1])
  expect_eq(10, args[2])
  expect_eq(nil, args[3])
end

local function test_exit_codes()         -- {{{1
  expect_lua("0\n", "return 0", 0)       -- success
  expect_lua(nil, "lua syntax error", 2) -- syntax error
  expect_lua(nil, "error('message')", 3) -- chunk error
end

local function test_spawn() -- {{{1
  expect_lua(nil, "vv.api.window_create_process('sh')")
  local _, bad_spawn = run_lua("vv.api.window_create_process('bad process')", 3)
  expect_match("No such file or directory", bad_spawn)
end

local function test_quit()    -- {{{1
  assert(server, "no test server !!")
  run_lua("vv.api.quit()", 5) -- 5: server exited
  expect_eq(0, server:wait_for_exit(100))
  server = nil
end

-- test {{{1

return function()
  start_server(servername)
  test_server_cli()
  test_exit_codes()
  test_spawn()
  test_quit()
end

-- Modeline {{{1
-- vim: fdm=marker shiftwidth=2 foldlevel=0
