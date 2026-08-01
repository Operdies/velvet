local M = {}
local async = vv.async

-- to ensure internal invariants, we dont' want to expose internals
-- to callers. Instead, we store local data and relations in weak tables.

-- the data associated with a process is keyed by the process,
-- so if the process instance is GC'd the data will disappear
--- @type table<velvet.process, velvet.process.private_data>
local process_data = setmetatable({}, { __mode = 'k' })
--- Process id's are also weakly linked to process instances,
--- so the callbacks become noops if the Process instance is gc'ed.
--- @type table<integer, velvet.process>
local id_to_process = setmetatable({}, { __mode = 'v' })

--- Streams are linked to their process id so stream functions
--- can call process-related functions and access internal stream buffers
--- @type table<velvet.process.input_stream|velvet.process.output_stream, integer>
local stream_to_process = setmetatable({}, { __mode = 'k' })

--- @class velvet.process
--- @field id integer
--- @field exit_code? integer
--- @field exit_signal? string
--- @field stdin? velvet.process.input_stream
--- @field stdout? velvet.process.output_stream
--- @field stderr? velvet.process.output_stream
local Process = {}
Process.__index = Process

--- @class velvet.process.output_stream
--- @field on_output velvet.async.event_source<string|nil> event signaled when new output is available
--- @field closed boolean true if the stream is closed
local OutputStream = {}
OutputStream.__index = OutputStream

--- @class velvet.process.input_stream
local InputStream = {}
InputStream.__index = InputStream

--- @class velvet.process.buffered_stream
--- @field [integer] string
--- @field head integer the first non-consumed entry
--- @field tail integer the last entry
--- @field cursor integer where to start checking for newlines
--- @field closed boolean
--- @field on_data velvet.async.event_source<string|nil>

--- @class velvet.process.private_data
--- @field stdout velvet.process.buffered_stream
--- @field stderr velvet.process.buffered_stream
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
function InputStream:write(data)
  local id = stream_to_process[self]
  vv.api.process_write_stdin(id, data)
end

function InputStream:close()
  local id = stream_to_process[self]
  vv.api.process_close_stdin(id)
end

--- @param buffer velvet.process.buffered_stream
--- @param data string
local function buffer_push(buffer, data)
  if data ~= nil then
    buffer.tail = buffer.tail + 1
    buffer[buffer.tail] = data
  else
    buffer.closed = true
  end
  buffer.on_data:emit(data)
end


--- @param buffer velvet.process.buffered_stream
local function buffer_consume_line(buffer)
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

--- @param buffer velvet.process.buffered_stream
local function buffer_consume_all(buffer)
  if buffer[buffer.tail] == nil then return nil end
  return table.concat(buffer, '', buffer.head, buffer.tail)
end

--- Read all remaining data from a stream.
---
--- This suspends the calling coroutine until the stream closes, typically when
--- the process exits or the stream is explicitly closed. Any data previously
--- consumed by |read_line| or another read operation is not returned.
---
--- All read operations consume data from a single read cursor per stream.
---
--- @return string|nil data Remaining data from the stream
function OutputStream:read_all()
  local id = stream_to_process[self]
  local proc = id_to_process[id]
  local data = process_data[proc]
  if not self.closed then
    -- Wait for the stream to close. The stream is closed when it returns nil.
    self.on_output:wait(nil, function(output) return output == nil end)
  end

  local stream = self == proc.stdout and 'stdout' or 'stderr'
  local buffer = data[stream]
  if buffer == nil then return nil end
  data[stream] = nil
  return buffer_consume_all(buffer)
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
--- @return string|nil line The next line, or nil if the stream closed before another complete line was available.
function OutputStream:line()
  local id = stream_to_process[self]
  local proc = id_to_process[id]
  local data = assert(process_data[proc])
  local stream = self == proc.stdout and 'stdout' or 'stderr'
  --- @type velvet.process.buffered_stream
  local buffer = data[stream]
  if buffer == nil then return nil end

  while not self.closed do
    local line = buffer_consume_line(buffer)
    if line then return line end
    -- no newline found -- wait for content
    self.on_output:wait()
  end
  -- stream closed -- return a line if possible, otherwise the rest of the data
  return buffer_consume_line(buffer) or self:read_all()
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
--- @return fun(): string|nil iterator
function OutputStream:lines()
  return function()
    return self:line()
  end
end

local function on_output(id, output, channel)
  local proc = id_to_process[id]
  if not proc then return end
  local private = process_data[proc]
  if not private then return end

  if output == nil then
    proc[channel].closed = true
  end

  local buffer = private[channel]
  buffer_push(buffer, output)
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
    stdin_pipe = opt.stdin,
    on_stdout = opt.stdout and on_output or nil,
    on_stderr = opt.stderr and on_output or nil,
    on_exit = on_exit,
  })

  local on_stdout = opt.stdout and async.event_source() or nil
  local on_stderr = opt.stderr and async.event_source() or nil

  local instance = setmetatable({ id = id }, Process)
  local private = {
    stdout = on_stdout and { head = 1, tail = 0, cursor = 1, on_data = on_stdout },
    stderr = on_stderr and { head = 1, tail = 0, cursor = 1, on_data = on_stderr },
    on_exit = async.event_source()
  }
  process_data[instance] = private

  local function stream(event)
    return setmetatable({
      owner = instance,
      closed = false,
      on_output = event:listener(),
    }, OutputStream)
  end

  if on_stdout then
    instance.stdout = stream(on_stdout)
    stream_to_process[instance.stdout] = instance.id
  end
  if on_stderr then
    instance.stderr = stream(on_stderr)
    stream_to_process[instance.stderr] = instance.id
  end
  if opt.stdin then
    instance.stdin = setmetatable({ owner = instance }, InputStream)
    stream_to_process[instance.stdin] = instance.id
  end

  id_to_process[id] = instance
  return instance
end

return M
