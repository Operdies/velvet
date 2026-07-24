local M = {}
local utils = require('utils')
local inspect = require('velvet.inspect')

--- @param spec spec
--- @return string _options.lua
function M.generate(spec)
  local builder = utils.builder()

  builder:push([[
error("Cannot require meta file")
--- @meta
--- @class velvet.options
local options = {}]])

  for _, fn in ipairs(spec.options) do
    local template = {
      doc = utils.string_concatenate(fn.doc, "\n--- "),
      type = utils.lua_type(fn.type),
      name = fn.name,
      default_value = type(fn.default) == 'table' and inspect(fn.default) or fn.default
    }
    builder:push([[
--- <doc>
--- @type <type>
options.<name> = <default_value>
]], template)
  end

  builder:push("return options\n")

  return builder:tostring()
end

return M
