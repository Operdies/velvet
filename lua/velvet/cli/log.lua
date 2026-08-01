--- registers a new cli action, `vv log`, which prints to stdout every time a log message is raised in velvet via print(), printerr(), or vv.log().
--- Note that the cli action itself uses print() to output;
--- This works because print() output is routed through vv.log() if the calling context is not associated with a cli action,
--- and back to the calling application if it is.

-- signaled after the log cli action starts waiting for events.
-- this is convenient for debug prints to start printing only
-- after a listener is actually attached.
-- The prime use case here is a shell loop such as:
-- $ while true; do vv log; done
-- which will miss messages logged before the socket reconnects

local log_connected = vv.async.event_source()
vv.cli.add_command({
  name = "log",
  description = "print all system messages",
  complete = function(...)
    local args = { ... }
    local set = {}
    for _, arg in ipairs(args) do set[arg] = true end
    if args[#args - 1] == '--level' then
      return {
        { name = "debug",   description = "include all messages" },
        { name = "info",    description = "include informational messages" },
        { name = "warning", description = "only show warnings and errors" },
        { name = "error",   description = "only show error messages" },
      }
    end

    local timestamp = { name = "--timestamp", description = "prepend messages with the current time" }
    local level = { name = "--level", description = "specify the level of messages to include" }

    local options = {}
    if not set['--timestamp'] then options[#options + 1] = timestamp end
    if not set['--level'] then options[#options + 1] = level end
    return options
  end,
  --- @async
  action = function(_, ...)
    local stamp = false
    local level = 'info'
    local severity = { debug = 1, info = 2, warning = 3, error = 4 }
    local args = { ... }
    for i = 1, #args do
      if args[i] == '--timestamp' then
        stamp = true
      elseif args[i] == '--level' then
        level = args[i + 1]
      end
    end
    local min_severity = severity[level] or error('invalid log level')
    -- indicate that we are listening after we hit the wait() yield
    vv.api.schedule_after(0, function() log_connected:emit() end)
    while true do
      local data = vv.async.wait_for_system_message()
      local msg = data.message
      if severity[data.level] >= min_severity then
        if stamp then
          local when = os.date("%H:%M:%S")
          msg = string.format("[%s] %s", when, msg)
        end
        local fn = data.level == 'error' and printerr or print
        fn(msg)
      end
    end
  end,
})

return {
  on_logger_connected = log_connected:listener()
}
