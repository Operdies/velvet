--- @class default_settings
--- @field prefix string prefix to insert default keybindings in. Defaults to <C-x>

local M = {
  --- @type default_settings
  settings = {
    prefix = "<C-x>"
  }
}

local function cfg()
  local vv = require('velvet')
  local keymap = require('velvet.keymap')
  local default_shell = os.getenv("SHELL") or "bash"

  local map = keymap.set
  local map_prefix = function(mapping, ...) map(M.settings.prefix .. mapping, ...) end

  map_prefix("r", vv.api.reload, { description = "Reload the lua context. This fully wipes out all lua state including windows, keybinds, and any state. Configs will be reloaded after restarting." })

  map_prefix("c", function()
    local focus = vv.api.get_focused_window()
    local cwd = vv.api.window_is_valid(focus) and vv.api.window_get_working_directory(focus) or
    vv.api.get_startup_directory()
    vv.api.window_create_process(default_shell, { working_directory = cwd })
  end, { description = "Create a new window running " .. default_shell })

  map_prefix("d", function() vv.api.session_detach(vv.api.get_active_session()) end,
    { description = "Detach the current terminal from the velvet session." })

  map_prefix(M.settings.prefix, function() vv.api.window_send_keys(vv.api.get_focused_window(), M.settings.prefix) end,
    { description = "Send the key <C-x> to the current window." })

  local dwm = require('velvet.layout.dwm')

  for i = 1, 9 do
    map_prefix(("%d"):format(i), function() dwm.toggle_tag(vv.api.get_focused_window(), i) end,
      { description = ("Toggle tag %d for the focused window."):format(i) })
    map_prefix(("<M-%d>"):format(i), function() dwm.toggle_view(i) end,
      { description = ("Toggle visibility of tag %d."):format(i) })
    map(("<M-%d>"):format(i), function() dwm.set_view(i) end,
      { description = ("Set currently visible tags to %d."):format(i) })
    map(("<M-S-%d>"):format(i), function() dwm.set_tags(vv.api.get_focused_window(), i) end,
      { description = ("Set tags to %d for the focused window."):format(i) })
  end

  map("<M-0>", function() dwm.set_view({ 1, 2, 3, 4, 5, 6, 7, 8, 9 }) end,
    { description = "Make all tags visible." })
  map("<S-M-0>", function() dwm.set_tags(vv.api.get_focused_window(), { 1, 2, 3, 4, 5, 6, 7, 8, 9 }) end,
    { description = "Make current window visible on all tags." })
  map_prefix("t", function() dwm.set_layer(vv.api.get_focused_window(), dwm.layers.tiled) end,
    { description = nil })
  map_prefix("f", function() 
    local win = vv.api.get_focused_window()
    dwm.set_layer(win, dwm.layers.floating) 
  end, { description = nil })

  local rect = require('velvet.ui.rect')

  local function move_and_resize(x, y, dx, dy)
    local win = vv.api.get_focused_window()
    local geom = rect.translate(rect.grow(vv.api.window_get_geometry(win), dx, dy), x, y)
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

  map("<M-right>", apply(move, 2, 0), { description = "Move window right" })
  map("<M-left>", apply(move, -2, 0), { description = "Move window left" })
  map("<M-S-right>", apply(resize, 2, 0), { description = "Make window wider" })
  map("<M-S-left>", apply(resize, -2, 0), { description = "Make window narrower" })
  map("<M-up>", apply(move, 0, -1), { description = "Move window up" })
  map("<M-down>", apply(move, 0, 1), { description = "Move window down" })
  map("<M-S-up>", apply(resize, 0, -1), { description = "Make window shorter" })
  map("<M-S-down>", apply(resize, 0, 1), { description = "Make window taller" })
  map_prefix("<C-j>", dwm.focus_next, { description = "Focus the next window.", repeatable = true })
  map_prefix("<C-k>", dwm.focus_prev, { description = "Focus the previous window.", repeatable = true })
  map_prefix("j", dwm.swap_next,
    { description = "Swap the next current window with the next tiled window.", repeatable = true })
  map_prefix("k", dwm.swap_prev,
    { description = "Swap the next current window with the previous tiled window.", repeatable = true })
  map_prefix("g", dwm.zoom, { description = "Move window to top of tiling stack." })
  map("<M-[>", apply(dwm.incmfact, -0.05), { description = "Make master stacking area narrower" })
  map("<M-]>", apply(dwm.incmfact, 0.05), { description = "Make master stacking area wider" })
  map("<M-i>", apply(dwm.incnmaster, 1), { description = "Increase number of windows in master stack" })
  map("<M-o>", apply(dwm.incnmaster, -1), { description = "Decrease number of windows in master stack" })
  map("<M-`>", dwm.select_previous_view, { description = "Select the previous view" })
  local ok, err = pcall(dwm.activate)
  if not ok then dbg({ dwm_activate = err }) end

  local function any_process_windows()
    for _, id in ipairs(vv.api.get_windows()) do
      if vv.api.window_is_valid(id) and not vv.api.window_is_lua(id) then return true end
    end
    return false
  end

  local event_manager = vv.events.create_group('default_config.close_if_all_exited', true)
  event_manager.window_closed = function()
    if not any_process_windows() then 
      vv.api.quit() 
    end
  end

  local function start_shell_if_no_windows()
    if not any_process_windows() then 
      vv.api.window_create_process(default_shell, { working_directory = vv.api.get_startup_directory() }) 
    end
  end

  -- start a shell after sourcing the user's config, but only if the user config did not create any windows.
  vv.api.schedule_after(0, function()
    start_shell_if_no_windows()
  end)
end

--- @param opt default_settings
function M.setup(opt)
  M.settings = vv.deepcopy(opt) -- todo: implement table merging
  cfg()
end

return M
