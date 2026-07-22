local M = {}
local async = vv.async

-- the data associated with a process is keyed by the process,
-- so if the process instance is GC'd the data will disappear
--- @type table<velvet.process, velvet.process.private_data>
local process_data = setmetatable({}, { __mode = 'k' })
--- Process id's are also weakly linked to process instances,
--- so the callbacks become noops if the Process instance is gc'ed.
--- @type table<integer, velvet.process>
local id_to_process = setmetatable({}, { __mode = 'v' })

--- @class velvet.process
--- @field id integer
--- @field exit_code? integer
--- @field exit_signal? string
local Process = {}
Process.__index = Process

--- @class velvet.process.buffered_stream
--- @field [integer] string
--- @field head integer the first non-consumed entry
--- @field tail integer the last entry
--- @field cursor integer where to start checking for newlines
--- @field closed boolean

--- @class velvet.process.private_data
--- @field stdout velvet.process.buffered_stream
--- @field stderr velvet.process.buffered_stream
--- @field on_stdout velvet.async.event_source<string|nil>
--- @field on_stderr velvet.async.event_source<string|nil>
--- @field on_exit velvet.async.event_source<integer|string>

--- @param timeout? integer
--- @return integer|string reason exit code or signal
function Process:wait_for_exit(timeout)
  if self.exit_code ~= nil then return self.exit_code end
  if self.exit_signal ~= nil then return self.exit_signal end
  local data = process_data[self]
  return data.on_exit:wait(timeout)
end

--- kill the running process
--- @param signal? velvet.api.unix_signal the signal to send, or 'term' if omitted.
function Process:kill(signal)
  vv.api.process_kill(self.id, signal)
end

--- write |data| to the process
--- @param data string
function Process:write_stdin(data)
  vv.api.process_write_stdin(self.id, data)
end

function Process:close_stdin()
  vv.api.process_close_stdin(self.id)
end

--- Read all remaining data from a stream.
---
--- This suspends the calling coroutine until the stream closes, typically when
--- the process exits or the stream is explicitly closed. Any data previously
--- consumed by |read_line| or another read operation is not returned.
---
--- All read operations consume data from a single read cursor per stream.
---
--- @param stream 'stdout'|'stderr'|nil the stream to read from, or 'stdout' if omitted
--- @return string|nil data Remaining data from the stream
function Process:read_all(stream)
  stream = stream or 'stdout'
  assert(stream == 'stdout' or stream == 'stderr')
  local data = assert(process_data[self])
  self:wait_for_exit()
  --- @type velvet.process.buffered_stream
  local buffer = data[stream]
  if buffer == nil then return nil end
  data[stream] = nil
  if buffer[buffer.tail] == nil then return nil end
  return table.concat(buffer, '', buffer.head, buffer.tail)
end

--- Read the next line from a stream.
---
--- This suspends the calling coroutine until a complete line is available or
--- the stream closes. The returned line does not include the trailing newline.
--- Each call consumes the returned line, so subsequent reads begin immediately
--- after it.
---
--- All read operations consume data from a single read cursor per stream.
---
--- @param stream 'stdout'|'stderr'|nil the stream to read from, or 'stdout' if omitted
--- @return string|nil line The next line, or nil if the stream closed before another complete line was available.
function Process:line(stream)
  stream = stream or 'stdout'
  assert(stream == 'stdout' or stream == 'stderr')
  local data = assert(process_data[self])
  --- @type velvet.process.buffered_stream
  local buffer = data[stream]
  if buffer == nil then return nil end
  if buffer.closed then error('cannot read from closed stream') end

  local function consume_line()
    for idx = buffer.cursor, buffer.tail do
      local entry = buffer[idx]
      local newline = entry:find('\n')
      if newline then
        -- newline found in existing string.
        -- 1. split this string on the newline and reinsert the portion after the newline.
        local pre_newline = entry:sub(1, newline - 1)
        local post_newline = entry:sub(newline + 1)
        buffer[idx] = pre_newline
        -- 2. join all parts up to this element
        local line = table.concat(buffer, '', buffer.head, idx)
        -- clear all entries we just joined
        for j = buffer.head, idx do buffer[j] = nil end

        local cursor = idx
        if post_newline == '' then
          -- if the line did not have trailing content, skip over it
          cursor = idx + 1
        else
          -- if the line had content after the newline, reinsert it
          buffer[idx] = post_newline
        end
        buffer.head = cursor
        buffer.cursor = cursor
        return line
      end
    end
    -- no newline found -- next scan will start from the tail
    buffer.cursor = buffer.tail + 1
  end

  local line = consume_line()
  if line then return line end

  local evt = stream == 'stdout' and data.on_stdout or data.on_stderr
  while true do
    -- no newline found -- wait for content
    local result = evt:wait()
    line = consume_line()
    if line then return line end
    if result == nil then
      -- stream closed, return the rest of the data. since line was nil, there are no newlines
      return self:read_all(stream)
    end
  end
end

--- Iterate all process output.
---
--- This suspends the calling coroutine until data is available. Any lines previously
--- consumed by |read_line| or another read operation are not returned.
--- The returned lines do not include the trailing newline.
---
--- All read operations consume data from a single read cursor per stream.
---
---```lua
---for c in Process:lines() do
---    body
---end
---```
---
--- @param stream 'stdout'|'stderr'|nil the stream to read from, or 'stdout' if omitted
--- @return fun(): string|nil iterator
function Process:lines(stream)
  return function()
    return self:line(stream)
  end
end

--- @param buffer velvet.process.buffered_stream
--- @param data string
local function buffer_push(buffer, data)
  if data ~= nil then
    buffer.tail = buffer.tail + 1
    buffer[buffer.tail] = data
  end
end

local function on_output(id, output, channel)
  local proc = id_to_process[id]
  if not proc then return end
  local data = process_data[proc]
  if not data then return end

  if channel == 'stdout' then
    local evt = data.on_stdout
    -- if output is nil, the event will never be raised again.
    -- nil the event to force an error if we try to wait for it again.
    if output == nil then data.on_stdout = nil end
    buffer_push(data.stdout, output)
    evt:emit(output)
  elseif channel == 'stderr' then
    local evt = data.on_stderr
    if output == nil then data.on_stderr = nil end
    buffer_push(data.stderr, output)
    evt:emit(output)
  end
end

local function on_exit(id, exit_code, signal)
  local proc = id_to_process[id]
  if not proc then return end
  local data = process_data[proc]
  if not data then return end
  proc.exit_code = exit_code
  proc.exit_signal = signal
  data.on_exit:emit(signal or exit_code)
end

--- @class velvet.process.spawn_options
--- @field environment? table<string, string>
--- @field working_directory? string
--- @field stdin? boolean
--- @field stdout? boolean
--- @field stderr? boolean

--- @param cmd string|string[]
--- @param options? velvet.process.spawn_options
--- @return velvet.process proc
function M.spawn(cmd, options)
  local opt = vv.deepcopy(options or {})
  assert(opt.stdin == nil or type(opt.stdin) == "boolean", "stdin must be a boolean")
  assert(opt.stdout == nil or type(opt.stdout) == "boolean", "stdout must be a boolean")
  assert(opt.stderr == nil or type(opt.stderr) == "boolean", "stderr must be a boolean")
  opt.stdin = opt.stdin ~= false
  opt.stdout = opt.stdout ~= false
  opt.stderr = opt.stderr ~= false
  local id = vv.api.process_spawn(cmd, {
    environment = opt.environment or nil,
    working_directory = opt.working_directory or nil,
    input = opt.stdin == false and "" or nil,
    on_stdout = opt.stdout and on_output or nil,
    on_stderr = opt.stderr and on_output or nil,
    on_exit = on_exit,
  })

  local instance = setmetatable({
    id = id,
  }, Process)
  process_data[instance] = {
    instance = instance,
    stdout = { head = 1, tail = 0, cursor = 1, closed = not opt.stdout },
    stderr = { head = 1, tail = 0, cursor = 1, closed = not opt.stderr },
    on_stdout = opt.stdout and async.event_source() or nil,
    on_stderr = opt.stdout and async.event_source() or nil,
    on_exit = async.event_source()
  }
  id_to_process[id] = instance
  return instance
end

return M
