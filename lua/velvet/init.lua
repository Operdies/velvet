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
  inspect = require('velvet.inspect'),
  --- @type velvet.api
  api = {},
  --- @type velvet.options
  options = {},
  --- Write string to the velvet log
  --- @param text string the string to log
  print = function(text) end,
  events = require('velvet.events'),
}

local function hex_to_rgb(hex)
  if type(hex) ~= "string" then
    return nil, "expected string"
  end

  -- Must be exactly "#rrggbb"
  if #hex ~= 7 or hex:sub(1, 1) ~= "#" then
    return nil, "invalid format (expected #rrggbb)"
  end

  -- Extract hex pairs
  local r = hex:sub(2, 3)
  local g = hex:sub(4, 5)
  local b = hex:sub(6, 7)

  -- Validate hex digits
  if not (r:match("^[%x][%x]$") and
        g:match("^[%x][%x]$") and
        b:match("^[%x][%x]$")) then
    return nil, "invalid hex digits"
  end

  return {
    red   = tonumber(r, 16),
    green = tonumber(g, 16),
    blue  = tonumber(b, 16),
  }
end

local color_palette = setmetatable({}, {
  __index = function(_, k)
    local tbl = vv.api.get_color_palette()
    return tbl[k]
  end,
  __newindex = function(_, k, v)
    local tbl = vv.api.get_color_palette()
    tbl[k] = v
    vv.options.color_palette = tbl
  end,
})

-- vv.api is populated in C. Here we use a metatable to direct
-- vv.options reads and assignments to the vv_api accessors
vv.options = setmetatable(vv.options, {
  __index = function(_, k)
    if k == 'color_palette' then
      -- return a special meta table for color palettes to allow
      -- setting individual colors directly, such as vv.options.color_palette.black = '#1e1e2e'
      return color_palette
    end
    return vv.api["get_" .. k]({})
  end,
  __newindex = function(_, k, v)
    if k == 'color_palette' then
      for key, col in pairs(v) do
        if type(col) == 'string' then
          local color, err = hex_to_rgb(col)
          if not color then error(err) end
          v[key] = color
        end
      end
    end
    return vv.api["set_" .. k](v, {})
  end,
})

--- global debug print function
function dbg(x) 
  local text = vv.inspect(x)
  print(text) 
  vv.events.emit_event('debug_log', x)
end

return vv
