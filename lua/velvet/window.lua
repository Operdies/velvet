--- @class velvet.window.border
--- @field left velvet.window left border
--- @field right velvet.window left border
--- @field top velvet.window left border
--- @field bottom velvet.window left border

--- @class velvet.window
--- @field id integer window handle
--- @field borders velvet.window.border borders
--- @field child_windows velvet.window[] child windows
local Window = {}

local vv = require('velvet')
local a = vv.api

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

--- @param color string
--- @return velvet.api.rgb_color
local function color_from_string(color)
  local palette = a.get_color_palette()
  if palette[color] then
    return palette[color]
  elseif type(color) == 'string' then
    local col, err = hex_to_rgb(color)
    if not col then error(err) end
    return col
  else
    return color
  end
end

--- @param self velvet.window
local function update_borders(self)
  if not self.frame_visible then return end
  if not self.borders then return end
  local l, r, t, b = self.borders.left, self.borders.right, self.borders.top, self.borders.bottom
  local geom = self:get_geometry()
  l:set_geometry({ left = geom.left - 1, top = geom.top, width = 1, height = geom.height })
  r:set_geometry({ left = geom.left + geom.width, top = geom.top, width = 1, height = geom.height })
  t:set_geometry({ left = geom.left - 1, top = geom.top - 1, width = geom.width + 2, height = 1 })
  b:set_geometry({ left = geom.left - 1, top = geom.top + geom.height, width = geom.width + 2, height = 1 })

  local vis = self:get_visibility()
  for _, brd in pairs(self.borders) do
    brd:set_visibility(vis)
    brd:set_z_index(self:get_z_index())
    if vis then
      brd:set_foreground_color(self.frame_color or color_from_string('black'))
      brd:clear_background_color()
      brd:clear()
    end
  end

  if not vis then return end

  local pipe = "│"
  local dash = "─"
  local dashes = string.rep(dash, geom.width + 2)
  local pipes = string.rep(pipe, geom.height)

  t:set_cursor(1, 1)
  t:draw(dashes)
  b:set_cursor(1, 1)
  b:draw(dashes)
  l:set_cursor(1, 1)
  l:draw(pipes)
  r:set_cursor(1, 1)
  r:draw(pipes)

  t:set_cursor(1, 1)
  t:draw('┌')
  t:set_cursor(geom.width + 2, 1)
  t:draw('┐')


  do
    local title_geom = t:get_geometry()
    local title_budget = title_geom.width - 4

    local scroll_height = vv.api.window_get_scrollback_size(self.id)
    local scroll_offset = vv.api.window_get_scroll_offset(self.id)

    if scroll_height > 0 and scroll_offset > 0 then
      local text = ('[%d of %d]'):format(scroll_offset, scroll_height)
      local start = title_geom.width - string.len(text) - 1
      if start > 0 then 
        title_budget = start - 3
        t:set_cursor(start, 1)
        t:draw(text)
      end
    end

    t:set_cursor(3, 1)
    local trunc = '…'
    local title = self:get_title()
    local codes = {}
    if title_budget > 0 then
      for p, c in utf8.codes(title) do
        if #codes + 1 >= title_budget then
          table.insert(codes, utf8.codepoint(trunc))
          break
        end
        table.insert(codes, c)
      end
      if #codes > 0 then
        t:draw(utf8.char(table.unpack(codes)))
      end
    end
  end

  b:set_cursor(1, 1)
  b:draw('└')
  b:set_cursor(geom.width + 2, 1)
  b:draw('┘')
end

local hooks = require('velvet.events').create_group('velvet_window_callback_manager', true)

hooks.window_created = function(win)
  win_registry[win.win_id] = Window.from_handle(win.win_id)
end

hooks.window_closed = function(win)
  local w = win_registry[win.win_id]
  if w then
    for _, child in ipairs(w.child_windows) do
      pcall(Window.close, child)
    end
    win_registry[win.win_id] = nil
  end
end

--- @param event string event name
--- @param args velvet.api.window.resized.event_args|velvet.api.window.on_key.event_args|velvet.api.window.moved.event_args
local function route_window_events(event, args)
  local win = win_registry[args.win_id]
  if win[event] then win[event](args) end
end

hooks.window_moved = function(evt) route_window_events('on_window_moved_handler', evt) end
hooks.window_resized = function(evt) route_window_events('on_window_resized_handler', evt) end
hooks.window_on_key = function(evt) route_window_events('on_window_on_key_handler', evt) end
hooks.pre_render = function() 
  for _, win in pairs(win_registry) do
    update_borders(win)
  end
end

--- @alias mouse_event
--- | 'mouse_click'
--- | 'mouse_move'
--- | 'mouse_scroll'

--- @param event mouse_event event name
--- @param args velvet.api.mouse.click.event_args | velvet.api.mouse.move.event_args | velvet.api.mouse.scroll.event_args
local function route_mouse_events(event, args)
  local win = args.win_id and args.win_id > 0 and win_registry[args.win_id]
  if win then
    if event == 'mouse_click' or vv.options.focus_follows_mouse then win:focus() end
      local geom = win:get_geometry()
      args.pos.col = args.pos.col - geom.left
      args.pos.row = args.pos.row - geom.top

      local window_func = 'on_' .. event .. '_handler'
      if win[window_func] then
        win[window_func](win, args)
      else
        vv.api['window_send_' .. event](args)
      end
  end
end

hooks.mouse_click = function(args) route_mouse_events('mouse_click', args) end
hooks.mouse_move = function(args) route_mouse_events('mouse_move', args) end
hooks.mouse_scroll = function(args) route_mouse_events('mouse_scroll', args) end

--- get window geometry
--- @return velvet.api.window.geometry
function Window:get_geometry()
  return a.window_get_geometry(self.id)
end

--- set window geometry
--- @param geom velvet.api.window.geometry
function Window:set_geometry(geom)
  a.window_set_geometry(self.id, geom)
end

--- Close the window. The instance wi
function Window:close()
  a.window_close(self.id)
end

--- Create a new window whose lifetime is tied to the parent window. If the parent window is closed, the child window is also closed. Otherwise, the windows are completely independent.
function Window:create_child_window()
  local chld = Window.create()
  ---@diagnostic disable-next-line: inject-field
  chld.parent = self
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

--- @return boolean window 
function Window:get_visibility()
  return not a.window_get_hidden(self.id)
end

--- @return boolean indicating whether this window is still valid.
function Window:valid()
  return a.window_is_valid(self.id)
end

--- Wrap an existing window
--- @param id integer window handle
--- @return velvet.window
function Window.from_handle(id)
  if not a.window_is_valid(id) then error(('%d is not a valid window.'):format(id)) end
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

local function set_ansimode(self, mode, on)
  self:draw(('\x1b[%d%s'):format(mode, on and 'h' or 'l'))
end

--- @param visible boolean if true, the cursor will be visible when the window is focused. This is useful for text-based windows
function Window:set_cursor_visible(visible)
  set_decmode(self, 25, visible)
end

--- @param line_wrapping boolean if true, the line will automatically wrap when text is written at the end of a line. This is useful for text, but bad for drawing.
function Window:set_line_wrapping(line_wrapping)
  set_decmode(self, 7, line_wrapping)
end

--- @param auto_return boolean if true, a newline will automatically insert a carriage return
function Window:set_auto_return(auto_return)
  set_ansimode(self, 20, auto_return)
end

--- @param x integer the new cursor column
--- @param y integer the new cursor row
function Window:set_cursor(x, y)
  self:draw(('\x1b[%d;%dH'):format(y, x))
end

--- @param alternate boolean if true, enter the alternate screen. Otherwise leave it
function Window:set_alternate_screen(alternate)
  set_decmode(self, 1047, alternate)
end

--- Create an automatically managed frame for the window. The frame will occupy one cell around the window.
--- @param enabled boolean set 
function Window:set_frame_enabled(enabled)
  self.frame_visible = enabled
  if enabled and not self.borders then
    self.borders = {
      left = self:create_child_window(),
      right = self:create_child_window(),
      top = self:create_child_window(),
      bottom = self:create_child_window(),
    }
    for _, brd in pairs(self.borders) do
      brd:set_alternate_screen(true)
      brd:set_cursor_visible(false)
      brd:on_mouse_move(function(_) 
        -- focus parent when mouse hovers borders
        if vv.options.focus_follows_mouse then
          self:focus() 
        end
      end)
    end
  end
end

--- @param color velvet.api.rgb_color|string the new foreground color
function Window:set_frame_color(color)
  if type(color) == 'string' then color = color_from_string(color) end
  self.frame_color = color
end

--- @return boolean
function Window:get_frame_enabled()
  return self.frame_visible
end

--- @param color velvet.api.rgb_color|string the new foreground color
function Window:set_foreground_color(color)
  if type(color) == 'string' then color = color_from_string(color) end
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
  if type(color) == 'string' then color = color_from_string(color) end
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

local function clamp(v, lo, hi)
  if v < lo then return lo end
  if v > hi then return hi end
  return v
end

--- @param mode 'none'|'clear'|'all'
function Window:set_transparency_mode(mode)
  local modes = { none = vv.api.transparency_mode.none, clear = vv.api.transparency_mode.clear, all = vv.api.transparency_mode.all }
  local integer = type(mode) == 'number' and mode or modes[mode]
  a.window_set_transparency_mode(self.id, integer)
end

--- @param dim number new dim factor. Highter dim means more dimming (0.0 - 1.0)
function Window:set_dimming(dim)
  a.window_set_dim_factor(self.id, dim)
end

--- @return number dim
function Window:get_dimming()
  return a.window_get_dim_factor(self.id)
end

--- @param opacity number Window opacity (0.0 - 1.0)
function Window:set_opacity(opacity)
  opacity = clamp(opacity, 0, 1)
  if opacity < 1 then
    local mode = a.window_get_transparency_mode(self.id)
    if mode == a.transparency_mode.none then 
      self:set_transparency_mode('all')
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

--- Focus this window
function Window:focus()
  a.set_focused_window(self.id)
end

function Window:clear()
  self:draw('\x1b[2J')
end

return Window
