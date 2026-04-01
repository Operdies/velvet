--- @param settings velvet.default_config.settings
local function cfg(settings)
  local pfx = settings.prefix or "<C-x>"
  local vv = require('velvet')
  local keymap = require('velvet.keymap')
  local default_shell = os.getenv("SHELL") or "bash"

  --- @param lhs string
  --- @param func fun()
  --- @param opt string|table
  local map = function(lhs, func, opt) keymap.set(lhs, func, type(opt) == 'table' and opt or { description = opt }) end

  local map_prefix = function(mapping, ...) map(pfx .. mapping, ...) end

  map_prefix("r", vv.api.reload, { description = "Reload config. Completely wipes global state." })

  map_prefix("c", function()
    local focus = vv.api.get_focused_window()
    local cwd = vv.api.window_is_valid(focus) and vv.api.window_get_working_directory(focus) or
    vv.api.get_startup_directory()
    vv.api.window_create_process(default_shell, { working_directory = cwd })
  end, { description = "Spawn " .. default_shell })

  map_prefix("d", function() vv.api.session_detach(vv.api.get_active_session()) end,
    { description = "Detach from velvet." })

  map_prefix(pfx, function() vv.api.window_send_keys(vv.api.get_focused_window(), pfx) end,
    { description = "Send the key <C-x> to the current window." })

  local dwm = require('velvet.layout.dwm')

  for i = 1, 9 do
    map_prefix(("%d"):format(i), function() dwm.toggle_tag(vv.api.get_focused_window(), i) end,
      { description = ("Toggle window tag %d."):format(i) })
    map_prefix(("<M-%d>"):format(i), function() dwm.toggle_view(i) end,
      { description = ("Toggle view %d."):format(i) })
    map(("<M-%d>"):format(i), function() dwm.set_view(i) end,
      { description = ("Select view %d."):format(i) })
    map(("<M-S-%d>"):format(i), function() dwm.set_tags(vv.api.get_focused_window(), i) end,
      { description = ("Set window tag %d."):format(i) })
  end

  map("<M-0>", function() dwm.set_view({ 1, 2, 3, 4, 5, 6, 7, 8, 9 }) end,
    { description = "Select all views." })
  map("<S-M-0>", function() dwm.set_tags(vv.api.get_focused_window(), { 1, 2, 3, 4, 5, 6, 7, 8, 9 }) end,
    { description = "Set all tags on window." })
  map_prefix("t", function() dwm.set_layer(vv.api.get_focused_window(), dwm.layers.tiled) end,
    { description = "Tile current window." })
  map_prefix("f", function() 
    local win = vv.api.get_focused_window()
    dwm.set_layer(win, dwm.layers.floating) 
  end, { description = "Float current window." })

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
  map_prefix("<C-j>", dwm.focus_next, { description = "Focus next window.", repeatable = true })
  map_prefix("<C-k>", dwm.focus_prev, { description = "Focus previous window.", repeatable = true })
  map_prefix("j", dwm.swap_next, { description = "Swap current and next window.", repeatable = true })
  map_prefix("k", dwm.swap_prev, { description = "Swap current and previous window.", repeatable = true })
  map_prefix("g", dwm.zoom, { description = "Move window to top of tiling stack." })
  map("<M-[>", apply(dwm.incmfact, -0.05), { description = "Make left stacking area narrower" })
  map("<M-]>", apply(dwm.incmfact, 0.05), { description = "Make left stacking area wider" })
  map("<M-i>", apply(dwm.incnmaster, 1), { description = "Increase number of windows in left stack" })
  map("<M-o>", apply(dwm.incnmaster, -1), { description = "Decrease number of windows in left stack" })
  map("<M-`>", dwm.select_previous_view, { description = "Select the previous view" })
  dwm.activate()

  local function any_process_windows()
    for _, id in ipairs(vv.api.get_windows()) do
      if vv.api.window_is_valid(id) and not vv.api.window_is_lua(id) and vv.api.window_get_parent(id) == 0 then return true end
    end
    return false
  end

  local event_manager = vv.events.create_group('default_config.shutdown', true)
  if settings.shutdown.on_last_window_exit then
    event_manager.window_closed = function()
      if not any_process_windows() then 
        vv.api.quit() 
      end
    end
  end

  local function start_shell_if_no_windows()
    if not any_process_windows() then 
      vv.api.window_create_process(default_shell, { working_directory = vv.api.get_startup_directory() }) 
    end
  end

  if settings.startup.spawn_shell then start_shell_if_no_windows() end
end

--- @class velvet.default_config.settings
--- @field prefix? string
--- @field shutdown? velvet.default_config.settings.shutdown
--- @field startup? velvet.default_config.settings.startup

--- @class velvet.default_config.settings.shutdown
--- @field on_last_window_exit? boolean

--- @class velvet.default_config.settings.startup
--- @field spawn_shell? boolean

--- @class velvet.default_config.settings
local default_settings = {
  prefix = "<C-x>",
  shutdown = { on_last_window_exit = true },
  startup = { spawn_shell = true },
}

return {
  --- @param opt? velvet.default_config.settings
  setup = function(opt)
    cfg(vv.tbl_deep_extend('force', default_settings, opt or {}))
  end
}
