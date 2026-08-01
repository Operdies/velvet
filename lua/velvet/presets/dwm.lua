--- @alias velvet.default_config.statusbar.builtin_modules
--- | 'clock'
--- | 'copy_mode'
--- | 'dwm_tags'
--- | 'servername'
--- | 'window_title'
--- | 'pending_keys'

local did_register_statusbar = false
local function register_statusbar_elements()
  if did_register_statusbar then return end
  did_register_statusbar = true

  local statusbar = require('velvet.extras.statusbar')

  local window = require('velvet.window')
  statusbar.register('window_title', {
    default_options = { foreground = '#000000', background = 'red', bold = true },
    ---@async
    content = function()
      local id = vv.api.get_focused_window()
      local win = vv.api.window_is_valid(id) and window.from_handle(id)
      if not win then return end
      local title = win:get_friendly_title()
      -- because default window titles are based on the process,
      -- the title is not always available immediately after the window
      -- is created. In this case, we can wait for a brief moment and check again.
      if not title then
        vv.async.wait(50)
        if win:valid() then
          title = win:get_friendly_title()
        end
      end
      return title
    end,
    update_triggers = { 'window_focus_changed' },
  })


  do
    local function tag_occupied(tags, tag)
      for _, set in pairs(tags) do
        if set[tag] then return true end
      end
      return false
    end

    local dwm = require('velvet.layout.dwm')
    statusbar.register('dwm_tags', {
      default_options = { active_background = 'red', inactive_background = 'blue', show_unoccupied_tags = false },
      update_triggers = { dwm.events.arrange },
      content = function(opt)
        --- @type velvet.statusbar.segment[]
        local segments = {}
        local state = dwm.get_state()
        for i, v in ipairs(state.view) do
          if v or opt.show_unoccupied_tags or tag_occupied(state.tags, i) then
            segments[#segments + 1] = {
              background = v and opt.active_background or opt.inactive_background,
              foreground = v and opt.active_foreground or opt.inactive_foreground,
              text = ' ' .. i .. ' ',
              bold = v,
              view_id = i, -- store the view id in the segment so we can use it in the on_click / on_mouse_move handlers.
            }
          end
        end
        if dwm.get_mode() == 'monocle' then segments[#segments+1] = { text = ' MONOCLE ', background = 'magenta' } end
        return segments
      end,
      -- change the currently visible view when the mouse hovers the segment,
      -- but only if the left mouse button is down
      on_mouse_move = function(opt, event_args)
        if opt.dragging then
          local seg = event_args.segment
          if seg and type(seg.view_id) == 'number' then
            if not event_args.move.modifiers.control then
              dwm.set_view(seg.view_id)
            end
          end
        end
      end,
      on_click = function(opt, event_args)
        if event_args.click.mouse_button == 'left' then
          -- track the mouse state so we can treat mouse move events differently when dragging with the left mouse.
          opt.dragging = event_args.click.event_type == 'mouse_down'
          if event_args.click.event_type == 'mouse_down' then
            local seg = event_args.segment
            if seg and type(seg.view_id) == 'number' then
              -- control click toggles a view,
              -- otherwise selects a view
              if event_args.click.modifiers.control then
                dwm.toggle_view(seg.view_id)
              else
                dwm.set_view(seg.view_id)
              end
            end
          else
            opt.dragging = false
          end
        end
      end,
    })
  end

  do
    local never = vv.async.event_source()
    statusbar.register('servername', {
      -- The server name never changes, so we never need to update this segment.
      -- statusbar handles empty or nil update_triggers by polling |content| every time the bar is being updated.
      -- To avoid this we instead wait for an object which will never be signaled.
      update_triggers = { never },
      default_options = { foreground = '#000000', background = 'red', bold = true },
      content = function() return vv.api.get_servername():upper() end,
    })
  end

  local keys = require('velvet.keymap')
  statusbar.register('pending_keys', {
    update_triggers = { keys.events.chain_changed },
    default_options = { foreground = 'white' },
    content = function(_, _, event)
      local chain = event and event.data or ''
      if chain ~= '' then
        return { { text = chain } }
      end
    end,
  })

  -- create a ticker which will tick once a minute on the minute
  local function minute_ticker()
    local ticker = vv.async.event_source()
    local listener = setmetatable({ ticker:listener() }, { __mode = 'v' })
    ---@async
    vv.async.run(function()
      -- stop ticking when the listener is garbage collected
      while listener[1] do
        local current_seconds = tonumber(os.date('%S'))
        local next_minute = (60 - current_seconds) * 1000
        vv.async.wait(next_minute)
        ticker:emit()
      end
    end)
    return listener[1]
  end

  statusbar.register('clock', {
    default_options = { foreground = '#000000', background = 'blue', bold = true },
    update_triggers = { minute_ticker() },
    content = function() return tostring(os.date('%H:%M')) end,
  })

  statusbar.register('copy_mode', {
    update_triggers = { require('velvet.copy_mode').events.mode_changed },
    default_options = { background = 'black', foreground = 'blue', bold = true },
    content = function(_, _, evt)
      local mode = evt and evt.data
      if not mode then return end
      if mode == 'lines' then
        mode = 'VISUAL LINE'
      elseif mode == 'block' then
        mode = 'VISUAL BLOCK'
      end
      return mode:upper()
    end,
  })
end

--- @param settings velvet.default_config.settings
local function cfg(settings)
  _G["VELVET_PRESET"] = 'velvet.presets.dwm'
  local pfx = settings.prefix or "<C-x>"
  local vv = require('velvet')
  local keymap = require('velvet.keymap')
  local default_shell = os.getenv("SHELL") or "bash"

  --- @param lhs string
  --- @param func fun()
  --- @param opt string|table
  local map = function(lhs, func, opt) keymap:set(lhs, func, type(opt) == 'table' and opt or { description = opt }) end

  local map_prefix = function(mapping, ...) map(pfx .. mapping, ...) end

  map_prefix("r", vv.api.reload, { description = "Reload config. Completely wipes global state." })
  map_prefix("h", function() require("velvet.shortcut-help").show() end, "Show shortcut help window")

  map_prefix("c", function() vv.api.window_create_process(default_shell, { working_directory = vv.cwd() }) end,
    { description = "Spawn " .. default_shell })

  map_prefix("d", function() vv.api.client_detach(vv.api.get_active_client()) end,
    { description = "Detach from velvet." })

  map_prefix(pfx, function() vv.api.window_send_keys(vv.api.get_focused_window(), pfx) end,
    { description = "Send the key <C-x> to the current window." })

  map_prefix('v', function() require('velvet.copy_mode').start() end, "Enter copy mode")

  local dwm = require('velvet.layout.dwm')

  for i = 1, 9 do
    map_prefix(("%d"):format(i), function() dwm.toggle_tag(vv.api.get_focused_window(), i) end,
      { description = "Toggle window tag {}." })
    map_prefix(("<M-%d>"):format(i), function() dwm.toggle_view(i) end,
      { description = "Toggle view {}." })
    map(("<M-%d>"):format(i), function() dwm.set_view(i) end,
      { description = "Select view {}." })
    map(("<M-S-%d>"):format(i), function() dwm.set_tags(vv.api.get_focused_window(), i) end,
      { description = "Set window tag {}." })
  end

  map("<M-0>", function() dwm.set_view({ 1, 2, 3, 4, 5, 6, 7, 8, 9 }) end,
    { description = "Select all views." })
  map("<S-M-0>", function() dwm.set_tags(vv.api.get_focused_window(), { 1, 2, 3, 4, 5, 6, 7, 8, 9 }) end,
    { description = "Set all tags on window." })
  map_prefix("t", function() dwm.set_layer(vv.api.get_focused_window(), 'tiled') end,
    { description = "Tile current window." })
  map_prefix("f", function()
    local win = vv.api.get_focused_window()
    dwm.set_layer(win, 'floating')
  end, { description = "Float current window." })

  local function apply(func, ...)
    local args = { ... }
    return function() func(table.unpack(args)) end
  end

  map_prefix("<C-j>", dwm.focus_next, { description = "Focus next window.", repeatable = true })
  map_prefix("<C-k>", dwm.focus_prev, { description = "Focus previous window.", repeatable = true })
  map_prefix("j", dwm.swap_next, { description = "Swap current and next window.", repeatable = true })
  map_prefix("k", dwm.swap_prev, { description = "Swap current and previous window.", repeatable = true })
  map_prefix("g", dwm.swap_main, { description = "Move window to top of tiling stack.", repeatable = true })
  map_prefix("[", apply(dwm.incmfact, -0.05), { description = "Make left stacking area narrower", repeatable = true })
  map_prefix("]", apply(dwm.incmfact, 0.05), { description = "Make left stacking area wider", repeatable = true })
  map_prefix("i", apply(dwm.incnmaster, 1), { description = "Increase number of windows in left stack", repeatable = true })
  map_prefix("o", apply(dwm.incnmaster, -1), { description = "Decrease number of windows in left stack", repeatable = true })
  map_prefix("z", dwm.toggle_monocle, { description = "Toggle monocle mode. Similar to tmux zoom mode." })

  local bar = nil
  if settings.statusbar then
    if settings.statusbar.location == 'top' then
      dwm.reserve(1, 0, 0, 0)
    else
      dwm.reserve(0, 0, 1, 0)
      settings.statusbar.location = 'bottom'
    end
    register_statusbar_elements()
    bar = require('velvet.extras.statusbar').create(settings.statusbar)
  end

  dwm.activate()

  local function any_process_windows()
    for _, id in ipairs(vv.api.get_windows()) do
      if vv.api.window_is_valid(id) and not vv.api.window_is_lua(id) and vv.api.window_get_parent(id) == nil then return true end
    end
    return false
  end

  if settings.shutdown.on_last_window_exit then
    local event_manager = vv.events.create_group('default_config.shutdown', true)
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

  return {
    statusbar = bar,
  }
end

--- @class velvet.default_config.settings
--- @field prefix? string
--- @field shutdown? velvet.default_config.settings.shutdown
--- @field startup? velvet.default_config.settings.startup
--- @field statusbar? velvet.statusbar.options

--- @class velvet.default_config.settings.shutdown
--- @field on_last_window_exit? boolean

--- @class velvet.default_config.settings.startup
--- @field spawn_shell? boolean

--- @class velvet.default_config.settings
local default_settings = {
  prefix = "<C-x>",
  shutdown = { on_last_window_exit = true },
  startup = { spawn_shell = true },
  statusbar = {
    left = {
      {
        -- some elements define custom options you can override,
        -- such as active/inactive colors for dwm_tags
        name = 'dwm_tags',
        options = {
          active_background = 'red',
          inactive_background = 'blue',
          foreground = '#000000',
          show_unoccupied_tags = false,
        }
      },
      {
        -- most elements will respect these four options:
        -- foreground, background, italic, bold
        name = 'pending_keys',
        options = {
          foreground = 'white',
          -- setting background to nil will cause the element to inherit the bar background
          background = nil,
          bold = false,
          italic = false,
        },
      },
      -- if the default options are fine, you can use the name as a shorthand, no table needed
      'copy_mode'
    },
    center = {  },
    right = { 'servername', 'clock' },
    -- bar position
    position = 'bottom',
    -- background color of unoccupied bar space.
    background = '#181825'
  },
}

--- @class velvet.presets.dwm
--- @field statusbar velvet.statusbar

return {
  --- @param opt? velvet.default_config.settings
  --- @return velvet.presets.dwm
  setup = function(opt)
    --- @type velvet.default_config.settings
    local settings = vv.tbl_deep_extend('force', default_settings, opt or {})
    if opt and opt.statusbar then
      -- deep_extend merges configuration keys, but statusbar segments should override
      for _, seg in ipairs { 'left', 'center', 'right' } do
        if opt.statusbar[seg] then settings.statusbar[seg] = opt.statusbar[seg] end
      end
    end
    return cfg(settings)
  end
}

