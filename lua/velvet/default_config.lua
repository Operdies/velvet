local vv = require('velvet')
local default_shell = os.getenv("SHELL") or "bash"

local map = vv.api.keymap_set
local rmap = function(keys, action) vv.api.keymap_set(keys, action, { repeatable = true }) end

map("<C-x>c", function() vv.api.window_create_process(default_shell) end)
map("<C-x>d", function() vv.api.session_detach(vv.api.get_active_session()) end)

map("<C-x><C-x>", function() vv.api.window_send_keys(vv.api.get_focused_window(), "<C-x>") end)

local dwm = require('velvet.layout.dwm')

for i = 1, 9 do
  map(("<C-x>%d"):format(i), function() dwm.toggle_tag(vv.api.get_focused_window(), i) end)
  map(("<C-x><M-%d>"):format(i), function() dwm.toggle_view(i) end)
  map(("<M-%d>"):format(i), function() dwm.set_view(i) end)
  map(("<M-S-%d>"):format(i), function() dwm.set_tags(vv.api.get_focused_window(), i) end)
end

map("<M-0>", function() dwm.set_view({ 1, 2, 3, 4, 5, 6, 7, 8, 9 }) end)
map("<S-M-0>", function() dwm.set_tags(vv.api.get_focused_window(), { 1, 2, 3, 4, 5, 6, 7, 8, 9 }) end)
map("<C-x>t", function() dwm.set_layer(vv.api.get_focused_window(), dwm.layers.tiled) end)
map("<C-x>f", function() 
  local win = vv.api.get_focused_window()
  dwm.set_layer(win, dwm.layers.floating) 
end)

local function translate(geom, x, y) 
  return { width = geom.width, height = geom.height, left = geom.left + x, top = geom.top + y }
end

local function scale(geom, dx, dy)
  return { width = geom.width + dx, height = geom.height + dy, left = geom.left, top = geom.top }
end

local function move_and_resize(x, y, dx, dy)
  local win = vv.api.get_focused_window()
  local geom = translate(scale(vv.api.window_get_geometry(win), dx, dy), x, y)
  dwm.set_layer(win, dwm.layers.floating) 
  local w = require('velvet.window').from_handle(win)
  w:set_geometry(geom)
end

local function resize(dx, dy)
  move_and_resize(0, 0, dx, dy)
end

local function move(x, y)
  move_and_resize(x, y, 0, 0)
end

local function apply(func, ...)
  local args = { ... }
  return function() func(table.unpack(args)) end
end

map("<M-right>", apply(move, 2, 0))
map("<M-left>", apply(move, -2, 0))
map("<M-S-right>", apply(resize, 2, 0))
map("<M-S-left>", apply(resize, -2, 0))
map("<M-up>", apply(move, 0, -1))
map("<M-down>", apply(move, 0, 1))
map("<M-S-up>", apply(resize, 0, -1))
map("<M-S-down>", apply(resize, 0, 1))
rmap("<C-x><C-j>", dwm.focus_next)
rmap("<C-x><C-k>", dwm.focus_prev)
rmap("<C-x>j", dwm.swap_next)
rmap("<C-x>k", dwm.swap_prev)
map("<C-x>g", dwm.zoom)
map("<M-[>", apply(dwm.incmfact, -0.05))
map("<M-]>", apply(dwm.incmfact, 0.05))
map("<M-i>", apply(dwm.incnmaster, 1))
map("<M-o>", apply(dwm.incnmaster, -1))

if #vv.api.get_windows() == 0 then
  vv.api.window_create_process(default_shell)
end
local ok, err = pcall(dwm.activate)
if not ok then dbg({ dwm_activate = err }) end
