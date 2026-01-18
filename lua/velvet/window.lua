--- @class velvet.window.border
--- @field left velvet.window left border
--- @field right velvet.window left border
--- @field top velvet.window left border
--- @field bottom velvet.window left border

--- @class velvet.window
--- @field id integer window handle
--- @field borders velvet.window[] borders
--- @field child_windows velvet.window[] child windows
local Window = {}

local a = require('velvet').api

Window.__index = Window

--- @type velvet.window[]
local win_registry = {}

--- @param hex string
--- @return velvet.api.rgb_color|nil,string|nil
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
  }, nil
end

--- @param evt any
--- @param win velvet.window
local function make_coords_local(evt, win)
  local geom = win:get_geometry()
  evt.pos.col = evt.pos.col - geom.left
  evt.pos.row = evt.pos.row - geom.top
  return evt
end

local hooks = require('velvet.events').create_group('velvet_window_callback_manager', true)

hooks.window_created = function(win)
  win_registry[win.id] = Window.from_handle(win.id)
end

hooks.window_closed = function(win)
  local w = win_registry[win.id]
  if w then
    for _, child in ipairs(w.child_windows) do 
      child:close()
    end
    win_registry[win.id] = nil
  end
end

local mappings = {
  mouse_click = 'on_mouse_click_handler',
  mouse_move = 'on_mouse_move_handler',
  mouse_scroll = 'on_mouse_scroll_handler',
  window_moved = 'on_window_moved_handler',
  window_resized = 'on_window_resized_handler',
  window_on_key = 'on_window_key_handler',
}

for event_name, callback_name in pairs(mappings) do
  hooks[event_name] = function(args)
    dbg(args)
    local win = win_registry[args.win_id]
    if args.pos and win then
      args = make_coords_local(args, win)
    end
    if win and win[callback_name] then
      win[callback_name](win, args)
    end
  end
end

local function ensure(id)
  if not a.window_is_valid(id) then error(('%d is not a valid window.'):format(id)) end
end

--- get window geometry
--- @return velvet.api.window.geometry
function Window:get_geometry()
  return a.window_get_geometry(self.id)
end

--- set window geometry
--- @param geom velvet.api.window.geometry
function Window:set_geometry(geom)
  return a.window_set_geometry(self.id, geom)
end

--- Close the window. The instance wi
function Window:close()
  a.window_close(self.id)
end

--- Create a new window whose lifetime is tied to the parent window. If the parent window is closed, the child window is also closed. Otherwise, the windows are completely independent.
function Window:create_child_window()
  local chld = Window.create()
  table.insert(self.child_windows, chld)
  return chld
end

--- @param title string
function Window:set_title(title)
  return a.window_set_title(self.id, title)
end

--- @return string
function Window:get_title()
  return a.window_get_title(self.id)
end

--- @param visible boolean window visibility
function Window:set_visibility(visible)
  a.window_set_hidden(self.id, not visible)
end

--- Wrap an existing window
--- @param id integer window handle
--- @return velvet.window
function Window.from_handle(id)
  ensure(id)
  if win_registry[id] then return win_registry[id] end
  local self = { id = id, child_windows = {} }
  local instance = setmetatable(self, Window)
  win_registry[id] = instance
  return instance
end

--- Create a new window
--- @return velvet.window
function Window.create()
  local win = Window.from_handle(a.window_create())
  return win
end

--- @param self velvet.window
--- @param mode integer
--- @param on boolean
local function set_decmode(self, mode, on)
  self:draw(('\x1b[?%d%s'):format(mode, on and 'h' or 'l'))
end

--- @param visible boolean if true, the cursor will be visible when the window is focused. This is useful for text-based windows
function Window:set_cursor_visible(visible)
  set_decmode(self, 25, visible)
end

--- @param line_wrapping boolean if true, the line will automatically wrap when text is written at the end of a line. This is useful for text, but bad for drawing.
function Window:set_line_wrapping(line_wrapping)
  set_decmode(self, 7, line_wrapping)
end

--- @param x integer the new cursor column
--- @param y integer the new cursor row
function Window:set_cursor(x, y)
  self:draw(('\x1b[%d;%dH'):format(y, x))
end

--- @param color velvet.api.rgb_color|string the new foreground color
function Window:set_foreground_color(color)
  local palette = a.get_color_palette()
  if palette[color] then
    color = palette[color]
  elseif type(color) == 'string' then
    local col, err = hex_to_rgb(color)
    if not col then error(err) end
    color = col
  end
  self:draw(('\x1b[38;2;%d;%d;%dm'):format(color.red, color.green, color.blue))
end

--- Set the current background color to the default terminal background
function Window:clear_background_color()
  self:draw('\x1b[49m')
end

--- Set the current foreground color to the default terminal foreground
function Window:clear_foreground_color()
  self:draw('\x1b[39m')
end

--- @param color velvet.api.rgb_color|string the new background color
function Window:set_background_color(color)
  local palette = a.get_color_palette()
  if palette[color] then
    color = palette[color]
  elseif type(color) == 'string' then
    local col, err = hex_to_rgb(color)
    if not col then error(err) end
    color = col
  end
  self:draw(('\x1b[48;2;%d;%d;%dm'):format(color.red, color.green, color.blue))
end

--- @param z integer new z index
function Window:set_z_index(z)
  a.window_set_z_index(self.id, z)
end

function Window:get_z_index()
  return a.window_get_z_index(self.id)
end

function Window:draw(str)
  a.window_write(self.id, str)
end

--- Enable window borders
function Window:enable_borders()
  if not self.borders then
    self.borders = {
      left = Window.create(),
      right = Window.create(),
      top = Window.create(),
      bottom = Window.create(),
    }
  end
end

function Window:disable_borders()
  if self.borders then
    for _, brd in pairs(self.borders) do
      brd:set_visibility(false)
    end
  end
end

local function clamp(v, lo, hi)
  if v < lo then return lo end
  if v > hi then return hi end
  return v
end

--- @param mode velvet.api.transparency_mode
function Window:set_transparency_mode(mode)
  a.window_set_transparency_mode(self.id, mode)
end

--- @param opacity number Window opacity (0.0 - 1.0)
function Window:set_opacity(opacity)
  opacity = clamp(opacity, 0, 1)
  if opacity < 1 then
    local mode = a.window_get_transparency_mode(self.id)
    if mode == a.transparency_mode.none then 
      self:set_transparency_mode(a.transparency_mode.all)
    end
  end
  a.window_set_opacity(self.id, opacity)
end

--- @param handler fun(self: velvet.window, args: velvet.api.mouse.click.event_args)
function Window:on_mouse_click(handler)
  self.on_mouse_click_handler = handler
end

--- @param handler fun(self: velvet.window, args: velvet.api.mouse.scroll.event_args)
function Window:on_mouse_scroll(handler)
  self.on_mouse_scroll_handler = handler
end

--- @param handler fun(self: velvet.window, args: velvet.api.mouse.move.event_args)
function Window:on_mouse_move(handler)
  self.on_mouse_move_handler = handler
end

--- @param handler fun(self: velvet.window, args: velvet.api.window.closed.event_args)
function Window:on_window_closed(handler)
  self.on_window_closed = handler
end

--- @param handler fun(self: velvet.window, args: velvet.api.window.moved.event_args)
function Window:on_window_moved(handler)
  self.on_window_moved = handler
end

--- @param handler fun(self: velvet.window, args: velvet.api.window.resized.event_args)
function Window:on_window_resized(handler)
  self.on_window_resized = handler
end

--- @param handler fun(self: velvet.window, args: velvet.api.window.on_key.event_args)
function Window:on_window_on_key(handler)
  self.on_window_on_key = handler
end

function Window:clear()
  self:draw('\x1b[2J')
end

return Window
