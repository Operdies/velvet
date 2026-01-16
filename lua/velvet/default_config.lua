local vv = require('velvet')
local default_shell = os.getenv("SHELL") or "bash"

local map = vv.api.keymap_set
local rmap = function(keys, action) vv.api.keymap_set(keys, action, { repeatable = true }) end

map("<C-x>c", function() vv.api.window_create_process(default_shell) end)
map("<C-x>d", function() vv.api.session_detach(vv.api.get_active_session()) end)

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

map("<M-right>", dwm.focus_next)
map("<M-left>", dwm.focus_prev)
map("<M-S-right>", dwm.swap_next)
map("<M-S-left>", dwm.swap_prev)
rmap("<C-x><C-j>", dwm.focus_next)
rmap("<C-x><C-k>", dwm.focus_prev)
rmap("<C-x>j", dwm.swap_next)
rmap("<C-x>k", dwm.swap_prev)
map("<C-x>g", dwm.zoom)

