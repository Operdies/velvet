---@class vv
local vv = {
  arrange_group_name = 'velvet.arrange',
  --- This is mostly a hint for managiging z indices
  layers = {
    --- windows which should appear behind other windows,
    --- such as wallpapers and desktop elements
    background = -10000,
    --- tiled windows, if a tiling layout scheme is used
    tiled = -1000,
    --- floating windows, if a layout scheme with stacking capabilities is used
    floating = 0,
    --- elements which require user attention
    popup = 1000,
  },
  -- stolen directly from vim.inspect
  inspect = require('velvet.inspect').inspect,
  --- @type velvet.api
  api = {},
  --- @type velvet.options
  options = {},
  --- Write string to the velvet log
  --- @param text string the string to log
  print = function(text) end,
  events = require('velvet.events'),
}

local function string_to_rgb(hex)
  if type(hex) ~= "string" then
    return nil, "expected string"
  end

  -- allow recursively looking up a color. This allows patterns such as setting 
  -- theme.cursor = 'red', where 'red' is automatically inferred as theme.red
  if vv.options.theme[hex] then return vv.options.theme[hex] end

  -- Must be "#rrggbb" or "#rrggbbaa"
  if (#hex ~= 7 and #hex ~= 9) or hex:sub(1, 1) ~= "#" then
    return nil, "invalid format (expected #rrggbb)"
  end

  -- Extract hex pairs
  local r = hex:sub(2, 3)
  local g = hex:sub(4, 5)
  local b = hex:sub(6, 7)
  local a = #hex > 7 and hex:sub(8, 9) or "00"

  -- Validate hex digits
  if not (r:match("^[%x][%x]$") and
        g:match("^[%x][%x]$") and
        b:match("^[%x][%x]$") and
        a:match("^[%x][%x]$")) then
    return nil, "invalid hex digits"
  end

  local color = {
    red   = tonumber(r, 16) / 255,
    green = tonumber(g, 16) / 255,
    blue  = tonumber(b, 16) / 255,
    alpha = tonumber(a, 16) / 255,
  }
  return color
end

local theme = setmetatable({}, {
  __index = function(_, k)
    local tbl = vv.api.get_theme()
    return tbl[k]
  end,
  __newindex = function(_, k, v)
    local tbl = vv.api.get_theme()
    tbl[k] = v
    vv.options.theme = tbl
  end,
})

-- vv.api is populated in C. Here we use a metatable to direct
-- vv.options reads and assignments to the vv_api accessors
vv.options = setmetatable(vv.options, {
  __index = function(_, k)
    if k == 'theme' then
      -- return a special meta table for color palettes to allow
      -- setting individual colors directly, such as vv.options.theme.black = '#1e1e2e'
      return theme
    end
    return vv.api["get_" .. k]({})
  end,
  __newindex = function(_, k, v)
    if k == 'theme' then
      for key, col in pairs(v) do
        if type(col) == 'string' then
          local color, err = string_to_rgb(col)
          if not color then error(err) end
          v[key] = color
        end
      end
    end
    return vv.api["set_" .. k](v, {})
  end,
})

--- @class inspect.format_options
--- @field depth? integer max recursion depth
--- @field newline? string newline separator
--- @field indent? string indent expression
--- @field process? fun(item: any, path: string[]): any processing function recursively applied to |x|

--- global debug print function
--- @param x any the object to log
--- @param options inspect.format_options|nil formatting options
function dbg(x, options) 
  local text = vv.inspect(x, options)
  print(text) 
  ---@diagnostic disable-next-line: invisible
  vv.events.emit_event('system_message', { message = text, level = 'debug' })
end

local global_error = error
error = function(message, level)
  -- level: set the call stack position to generate debug information from
  level = (level or 1) + 2
  local _, err = pcall(global_error, message, level)
---@diagnostic disable-next-line: invisible
  vv.events.emit_event('system_message', { message = err, level = 'error' })
  global_error(message, level)
end

return vv
