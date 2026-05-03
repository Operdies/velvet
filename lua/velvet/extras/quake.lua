--- @class velvet.quake
--- @field hide fun()
--- @field show fun()
--- @field toggle fun()
--- @field visible boolean
--- @field cmd string|string[]

local quake_registry = {}

-- smoother transition when switching between quakes.
-- focused quakes should be shown just above non-focused quakes.
local focused_z = vv.z_hint.popup - 1
local unfocused_z = focused_z - 1

--- @param cmd string|string[] program to run
--- @param id? string Unique name of this instance. Used to restore state after reloading. If no id is provided, the instance is lost after reloading.
--- @return velvet.quake instance
local function quake_builder(cmd, id)
  local window = require('velvet.window')
  local instance = { cmd = cmd, visible = false }
  --- @type velvet.window
  local quakeHost = nil
  --- @type velvet.window
  local quake = nil

  local function calculate_size()
    local screen_size = vv.api.get_screen_geometry()
    local minwidth = 3
    local width = screen_size.width
    if width < minwidth then width = minwidth end
    local height = screen_size.height // 3
    local minheight = math.min(40, screen_size.height)
    if height < minheight then height = minheight end
    return {
      width = width,
      height = height,
      left = 1,
      top = 1 + screen_size.height - height,
    }
  end

  local function setsize()
    if quake and quake:valid() then
      quake:set_frame_enabled(true)
      quake:set_frame_color('magenta')
      quake:set_title(id or "Quake")
      quake:set_alpha(0.7)
      quakeHost:set_background_color('black')
      quakeHost:set_alpha(0.1)
      quakeHost:set_z_index(quake:get_z_index() - 1)
      local winSize = calculate_size()
      quake:set_geometry(winSize)
      quakeHost:clear()
    end
  end

  local prevFocus = nil

  local anim = require('velvet.ui.animation')
  local opts = { easing_function = anim.easing.spring }

  local anim_duration = 200
  local function hide(duration)
    if prevFocus and vv.api.window_is_valid(prevFocus) and not quake_registry[prevFocus] then
      vv.api.set_focused_window(prevFocus)
      prevFocus = nil
    end

    local screen = vv.api.get_screen_geometry()
    local new_size = calculate_size()
    new_size.top = screen.height
    quake:set_z_index(unfocused_z)
    vv.async.run(function()
      if anim.animate(quake.id, new_size, duration or anim_duration, opts) then
        quake:set_visibility(false)
        quakeHost:set_visibility(false)
      end
    end)
    instance.visible = false
  end

  local function show(duration)
    duration = duration or anim_duration
    local focus = vv.api.get_focused_window()
    if focus ~= quake.id and not quake_registry[focus] then prevFocus = focus end
    quake:set_visibility(true)
    quakeHost:set_visibility(true)
    quake:focus()
    quake:set_z_index(focused_z)
    local new_size = calculate_size()
    vv.async.run(anim.animate, quake.id, new_size, duration, opts)
    instance.visible = true
  end

  local runtime = {}
  if id then
  local key = 'velvet.quake_state.' .. id
    runtime = require('velvet.runtime_storage').create(key)
  end

  local function create_quake(win)
    quakeHost = window.create()
    local cwd = nil
    local fg = vv.api.get_focused_window()
    if fg ~= 0 then cwd = vv.api.window_get_working_directory(fg) end
    if win then
      vv.api.window_set_parent(win.id, quakeHost.id)
      quake = win
    else
      quake = quakeHost:create_child_process_window(cmd, { working_directory = cwd })
    end
    if not quake then
      quakeHost:close()
      error("Unable to create quake window: could not spawn shell.")
    end
    quake_registry[quake.id] = true
    quake_registry[quakeHost.id] = true
    -- quake_evt.screen_resized = setsize
    quake:set_visibility(false)
    quakeHost:set_visibility(false)

    quake:on_window_closed(function()
      if quake and quake.id then quake_registry[quake.id] = nil end
      if quakeHost and quakeHost.id then
        quake_registry[quakeHost.id] = nil
        quakeHost:close()
      end
    end)
    quake:on_window_moved(function(_, args) quakeHost:set_geometry(args.new_size) end)
    quake:on_focus_changed(function(_, args)
      if args.new == quake then show() end
      if args.old == quake then hide() end
      if args.new == quake then
        if args.old and not quake_registry[args.old.id] then
          -- don't track other quake windows for the purposes
          -- of restoring focus. Otherwise, toggling quake windows will just
          -- juggle focus between them which is rarely intended.
          prevFocus = args.old.id
        end
      end
    end)
    setsize()
  end

  instance.toggle = function()
    if quake == nil or not quake:valid() then
      instance.visible = false
      if quakeHost and quakeHost:valid() then quakeHost:close() end
      create_quake()
      -- hide with animations disable so the initial open will also slide in from the bottom
      hide(0)
    end
    if instance.visible then hide() else show() end
  end

  if runtime.quake and vv.api.window_is_valid(runtime.quake) then
    instance.visible = runtime.visible
    create_quake(window.from_handle(runtime.quake))
    quakeHost:set_geometry(quake:get_geometry())
    if instance.visible then show(0) else hide(0) end
  end

  vv.async.run(function()
    local resized = 'screen.resized'
    local reloaded = 'pre_reload'
    for reg, _ in vv.async.stream(resized, reloaded) do
      if reg == resized then
        pcall(setsize)
      elseif reg == reloaded then
        if id and quake and quake:valid() then
          -- hack: set the window as its own parent so dwm will not try to manage it.
          -- Once the runtime is reloaded, quakeHost will adopt it again
          vv.api.window_set_parent(quake.id, quake.id);
          if quake and quake:valid() and quake.id then
            runtime.quake = quake.id
            runtime.visible = instance.visible
          end
        end
      end
    end
  end)
  return instance
end

return {
  create = quake_builder
}
