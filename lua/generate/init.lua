local out_dir = assert(arg[1], "usage: lua generate.lua <out_dir>")
out_dir = out_dir:gsub("/$", "", 1)

package.path = './lua/?.lua;./lua/?/init.lua;./lua/generate/?.lua;./lua/generate/?/init.lua;'

local utils = require('utils')
local spec = utils.spec()


local function ensure_dir(path)
  return os.execute(string.format('mkdir -p "%s"', path))
end

local function write_file(path, contents)
  local f = assert(io.open(path, "w"))
  f:write(contents)
  f:close()
end

ensure_dir(out_dir)

-- map a generators to their output files
local gen = {
  ['velvet_api_h'] = out_dir .. '/velvet_api.h',
  ['velvet_enum_converters_c'] = out_dir .. "/velvet_enum_converters.c",
  ['velvet_lua_bindings_c'] = out_dir .. "/velvet_lua_bindings.c",
  ['velvet_lua_event_emitters_c'] = out_dir .. "/velvet_lua_event_emitters.c",
  ['api_meta'] = 'lua/velvet/_api.lua',
  ['options_meta'] = 'lua/velvet/_options.lua',
  ['default_options'] = 'lua/velvet/default_options.lua',
  ['async_wait_functions'] = 'lua/velvet/async/wait_functions.lua',
}

-- generate
for input, output in pairs(gen) do
  local text = require(input).generate(spec)
  write_file(output, text)
  print(output)
end
