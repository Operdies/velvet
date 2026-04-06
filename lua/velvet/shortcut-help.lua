local M = {}
local winapi = require('velvet.window')
local w = winapi.create()
local e = vv.events.create_group("velvet.which_key", true)
local km = require('velvet.keymap')

w:set_z_index(10000)
w:set_title("Shortcuts")
w:set_cursor_visible(false)
w:set_frame_color('green')
w:set_frame_enabled(true)

local arrow = " ➤ "

local shown = false
e.session_on_key = function(args)
  -- create a new event listener to close the window on *any* keypress.
  -- This is done to handle the case where focus is changed with the mouse
  -- and then typing.
  if shown and args.key.event_type == 'press' or args.key.event_type == 'repeat' then
    local k = args.key.name
    if k == 'k' then
      vv.api.window_set_scroll_offset(w.id, vv.api.window_get_scroll_offset(w.id) + 1)
    elseif k == 'j' then
      vv.api.window_set_scroll_offset(w.id, vv.api.window_get_scroll_offset(w.id) + -1)
    else
      -- schedule: inhibit the key event which closed the window.
      -- This is needed because session_on_key is more low level than window_on_key,
      -- so it may not have been routed to the help window yet. By scheduling the hide() call,
      -- we ensure it is swallowed only if the help window would be the target.
      vv.api.schedule_after(0, M.hide)
    end
  end
end

local function flatten_maps(map)
  local flat = {}
  if map.terminal then return { { text = map.keys, keys = { map.keys .. (map.repeatable and " (repeats)" or "") }, description = map.description } } end
  for _, m in ipairs(map.children) do
    for _, sub in ipairs(flatten_maps(m)) do
      local text = #map.keys > 0 and map.keys or ""
      if #map.keys > 0 then
        sub.text = text .. arrow .. sub.text
        table.insert(sub.keys, 1, map.keys)
      end
      flat[#flat + 1] = sub
    end
  end
  return flat
end

local function draw()
  local root = km.which_key("", true)
  local flat_map = flatten_maps({ keys = "", children = root })
  local sz = vv.api.get_screen_geometry()
  local width = math.floor(sz.width * 0.7)
  local height = math.floor(sz.height * 0.8)
  w:draw("\x1b[2J\x1b[3J")
  w:clear()
  w:set_cursor(1, 1)
  w.bottom_text = "j ▼  k ▲"

  local columns = {}
  local max_desc = 0
  for _, map in ipairs(flat_map) do
    for i, k in ipairs(map.keys) do
      local cur = columns[i] or 0
      columns[i] = math.max(cur, vv.api.string_display_width(k))
    end
    max_desc = math.max(max_desc, vv.api.string_display_width(map.description))
  end

  local display_width = 0
  for _, col in ipairs(columns) do
    display_width = display_width + col + #arrow
  end

  width = math.min(width, display_width + max_desc)
  w:set_geometry({ left = 1 + sz.width // 2 - width // 2, top = 1 + sz.height // 2 - height // 2, width = width, height = height })

  for _, map in ipairs(flat_map) do
    local _end = 0
    w:draw("\x1b[0;1m")
    w:set_foreground_color('blue')
    for i, k in ipairs(map.keys) do
      local kw = vv.api.string_display_width(k)
      local pad = string.rep(' ', columns[i] - kw)
      if i > 1 then w:draw(arrow) end
      w:draw(k .. pad)
      _end = i + 1
    end
    for i = _end, #columns do
      w:draw(string.rep(' ', 3 + columns[i]))
    end
    w:draw(arrow)
    w:draw("\x1b[0;3m")
    w:draw(map.description .. '\r\n')
  end
end

e[km.keymap_changed] = draw
e.screen_resized = draw

local prevfocus = nil
M.show = function()
  shown = true
  w:set_visibility(true)
  prevfocus = vv.api.get_focused_window()
  w:focus()
  draw()
  vv.api.window_set_scroll_offset(w.id, 100000)
end
M.hide = function()
  shown = false
  w:set_visibility(false)
  if prevfocus and vv.api.window_is_valid(prevfocus) then
    vv.api.set_focused_window(prevfocus)
    prevfocus = nil
  end
end
M.hide()
return M
