---@class vv
local vv = {
  --- This is a hint for managiging z indices.
  --- They are defined at this level so different window managers can make a reasonable
  --- guess about Z planes to use so user-defined windows (popups, statusbars, etc.)
  --- can work with different window managers.
  z_hint = {
    --- windows which should appear behind other windows,
    --- such as wallpapers and desktop elements. Works best when windows in higher layers 
    background = -100000,
    --- tiled windows, if a tiling layout scheme is used
    tiled = -10000,
    --- tstatus bars, power lines, etc. Should appear above tiled windows but below floating windows and popups
    statusbar = -1000, 
    --- floating windows, if a layout scheme with stacking capabilities is used
    floating = 1000,
    --- elements which require user attention. Should appear above everything else.
    popup = 10000,
    --- Special overlays which should affect everything. One example of this is the mouse-copy implementation
    --- which tints all content and intercepts mouse input
    overlay = 100000,
  },

  --- Emit an error message to all system_message listeners
  --- @param error_message string error message
  system_error = function(error_message)
    ---@diagnostic disable-next-line: invisible
    vv.events.emit_event('system_message', { message = error_message, level = 'error' })
  end,

  -- stolen from vim.inspect
  inspect = require('velvet.inspect').inspect,

  --- Recursively copy the fields and tables of |tbl| and return the copy.
  ---@param tbl table table to copy
  ---@return table copy copied table
  deepcopy = function(tbl)
    if type(tbl) ~= 'table' then return tbl end
    local copy = {}
    for k, v in pairs(tbl) do
      copy[vv.deepcopy(k)] = vv.deepcopy(v)
    end
    return setmetatable(copy, getmetatable(tbl))
  end,

  tbl_deep_extend = require('velvet.lib.deep_extend'),

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
  local a = #hex > 7 and hex:sub(8, 9) or "ff"

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
  local text = type(x) == 'string' and x or vv.inspect(x, options)
  ---@diagnostic disable-next-line: invisible
  vv.events.emit_event('system_message', { message = text, level = 'debug' })
end

return vv
