local vv = require('velvet')
local default_shell = os.getenv("SHELL") or "bash"
local o = vv.options

local function num_to_tags(num)
  return 1 << (num - 1)
end

local prev_view = 1
local function set_view(view)
  if type(view) == 'number' then
    view = { view }
  end
  local bits = 0
  for _, num in ipairs(view) do
    bits = bits | num_to_tags(num)
  end
  prev_view = o.view
  vv.api.set_view(bits)
end

local function restore_view()
  local current = o.view
  vv.api.set_view(prev_view)
  prev_view = current
end

local function set_tags(tags)
  if type(tags) == 'number' then
    tags = { tags }
  end
  local bits = 0
  for _, num in ipairs(tags) do
    bits = bits | num_to_tags(num)
  end
  vv.api.window_set_tags(vv.api.get_focused_window(), bits)
end

local function toggle_view(num)
  local view = o.view
  prev_view = view
  view = view ~ num_to_tags(num)
  o.view = view
end

local function toggle_tag(num)
  local tags = vv.api.window_get_tags(vv.api.get_focused_window())
  tags = tags ~ num_to_tags(num)
  vv.api.window_set_tags(0, tags)
end

local function window_visible(id)
  return (vv.api.window_get_tags(id) & o.view) > 0
end

local function window_tiled(id)
  return vv.api.window_get_layer(id) == "tiled"
end

local function get_prev_matching(id, match)
  local windows = vv.api.get_windows()
  local pivot = -1
  for i, win in ipairs(windows) do
    if win == id then
      pivot = i - 1
      break
    end
  end
  for i=#windows-1,0,-1 do
    local index = 1 + ((i + pivot) % (#windows))
    if match(windows[index]) then return windows[index] end
  end
  return nil
end

local function get_first_matching(match)
  local windows = vv.api.get_windows()
  for _, id in ipairs(windows) do 
    if match(id) then return id end
  end
  return nil
end

local function get_next_matching(id, match)
  local windows = vv.api.get_windows()
  local pivot = -1
  for i, win in ipairs(windows) do
    if win == id then
      pivot = i - 1
      break
    end
  end
  for i=1,#windows do
    local index = 1 + ((i + pivot) % (#windows))
    if match(windows[index]) then return windows[index] end
  end
  return nil
end

local function focus_next()
  local current = vv.api.get_focused_window()
  local next = get_next_matching(current, window_visible)
  if next then vv.api.set_focused_window(next) end
end

local function focus_prev()
  local current = vv.api.get_focused_window()
  local prev = get_prev_matching(current, window_visible)
  if prev then vv.api.set_focused_window(prev) end
end

local animation = require('velvet.stdlib.animation')
local currently_animating = false
local function swap(a, b)
  if a and b and a ~= b and not currently_animating then
    local l1 = vv.api.window_get_layer(a)
    local l2 = vv.api.window_get_layer(b)
    local g1 = vv.api.window_get_geometry(a)
    local g2 = vv.api.window_get_geometry(b)

    local i = 2
    -- only set window layers after both windows were fully moved
    local done = function()
      currently_animating = false
      i = i - 1
      if i == 0 then
        vv.api.window_set_layer(a, l2)
        vv.api.window_set_layer(b, l1)
      end
    end
    currently_animating = true
    animation.animate(a, g2, 200, { easing_function = animation.easing.spring, on_completed = done })
    animation.animate(b, g1, 200, { easing_function = animation.easing.spring, on_completed = done })
    vv.api.swap_windows(a, b)
  end
end

local function swap_prev()
  local current = vv.api.get_focused_window()
  local prev = get_prev_matching(current, function(w) return window_visible(w) end)
  swap(current, prev)
end

local function swap_next()
  local current = vv.api.get_focused_window()
  local next = get_next_matching(current, function(w) return window_visible(w) end)
  swap(current, next)
end

local function zoom()
  local current = vv.api.get_focused_window()
  local next = get_first_matching(function(w)
    return window_visible(w) and window_tiled(w) and w ~= current
  end)
  swap(current, next)
  local first_tiled = get_first_matching(function(w) return w == current or w == next end)
  if first_tiled then
    vv.api.set_focused_window(first_tiled)
  end
end

local map = vv.api.keymap_set
local rmap = function(keys, action) vv.api.keymap_set(keys, action, { repeatable = true }) end

map("<M-right>", focus_next)
map("<M-left>", focus_prev)
map("<M-S-right>", swap_next)
map("<M-S-left>", swap_prev)
rmap("<C-x><C-j>", focus_next)
rmap("<C-x><C-k>", focus_prev)
rmap("<C-x>j", swap_next)
rmap("<C-x>k", swap_prev)
rmap("<C-x>g", zoom)
rmap("<D-g>", zoom)

map("<C-x>c", function() vv.api.window_create_process(default_shell) end)
map("<C-x>d", function() vv.api.session_detach(vv.api.get_active_session()) end)

map("<M-`>", restore_view)
map("<C-x><C-x>", function() vv.api.window_send_keys(vv.api.get_focused_window(), "<C-x>") end)

local dwm = require('velvet.layout.dwm')
dwm.activate()

for i = 1, 9 do
  map(("<C-x>%d"):format(i), function() dwm.toggle_tag(vv.api.get_focused_window(), i) end)
  map(("<C-x><M-%d>"):format(i), function() dwm.toggle_view(i) end)
  map(("<M-%d>"):format(i), function() dwm.set_view(i) end)
  map(("<M-S-%d>"):format(i), function() dwm.set_tags(vv.api.get_focused_window(), i) end)
end

map("<M-0>", function() dwm.set_view({ 1, 2, 3, 4, 5, 6, 7, 8, 9 }) end)
map("<S-M-0>", function() dwm.set_tags(vv.api.get_focused_window(), { 1, 2, 3, 4, 5, 6, 7, 8, 9 }) end)
map("<C-x>t", function() dwm.set_layer(vv.api.get_focused_window(), "tiled") end)
map("<C-x>f", function() 
  local win = vv.api.get_focused_window()
  vv.api.window_set_geometry(win, { left = 10, top = 10, width = 30, height = 14 })
  dwm.set_layer(win, "floating") 
end)
