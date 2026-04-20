local window = require('velvet.window')
local quake_evt = vv.events.create_group("quake_resize_event", true)

--- @type velvet.window
local quakeHost = nil
--- @type velvet.window
local quake = nil

local function get_size()
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
    -- put quake right below popups
    quake:set_z_index(vv.z_hint.popup - 1)
    quake:set_title("Quake")
    quake:set_alpha(0.7)
    quakeHost:set_background_color('black')
    quakeHost:set_alpha(0.1)
    quakeHost:set_z_index(quake:get_z_index() - 1)
    local winSize = get_size()
    quake:set_geometry(winSize)
    quakeHost:clear()
  end
end

local prevFocus = nil
local visible = false

local anim = require('velvet.ui.animation')
local opts = { easing_function = anim.easing.spring }

local anim_duration = 200
local function hide(duration)
  if prevFocus and vv.api.window_is_valid(prevFocus) then
    vv.api.set_focused_window(prevFocus)
    prevFocus = nil
  end

  local screen = vv.api.get_screen_geometry()
  local new_size = get_size()
  new_size.top = screen.height
  vv.async.run(function() 
    if anim.animate(quake.id, new_size, duration or anim_duration, opts) then
      quake:set_visibility(false)
      quakeHost:set_visibility(false)
    end
  end)
  visible = false
end

local function show(duration)
  duration = duration or anim_duration
  local focus = vv.api.get_focused_window()
  if focus ~= quake.id then prevFocus = focus end
  quake:set_visibility(true)
  quakeHost:set_visibility(true)
  quake:focus()
  local new_size = get_size()
  vv.async.run(anim.animate, quake.id, new_size, duration, opts)
  visible = true
end

local runtime = require('velvet.runtime_storage').create("velvet.quake_state")
vv.async.run(function()
  vv.async.wait('pre_reload')
  if quake and quake:valid() then
    -- hack: set the window as its own parent so dwm will not try to manage it.
    -- Once the runtime is reloaded, quakeHost will adopt it again
    vv.api.window_set_parent(quake.id, quake.id);
    runtime.quake = quake.id
    runtime.visible = visible
  end
end)

local function create_quake(win)
  quakeHost = window.create()
  local cwd = nil
  local fg = vv.api.get_focused_window()
  if fg ~= 0 then cwd = vv.api.window_get_working_directory(fg) end
  if win then
    vv.api.window_set_parent(win.id, quakeHost.id)
    quake = win
  else
    quake = quakeHost:create_child_process_window("zsh", { working_directory = cwd })
  end
  if not quake then 
    quakeHost:close()
    error("Unable to create quake window: could not spawn shell.")
  end
  quake_evt.screen_resized = setsize
  quake:set_visibility(false)
  quakeHost:set_visibility(false)

  quake:on_window_closed(function() quakeHost:close() end)
  quake:on_window_moved(function(_, args) quakeHost:set_geometry(args.new_size) end)
  quake:on_focus_changed(function(_, args)
    if args.new == quake then show() end
    if args.new == quake then
      if args.old and args.old ~= quake then prevFocus = args.old.id end
    end
  end)
  setsize()
end

local function toggle()
  if quake == nil or not quake:valid() then
    visible = false
    if quakeHost and quakeHost:valid() then quakeHost:close() end
    create_quake()
    -- hide with animations disable so the initial open will also slide in from the bottom
    hide(0)
  end
  if visible then hide() else show() end
end

if runtime.quake and vv.api.window_is_valid(runtime.quake) then
  visible = runtime.visible
  create_quake(window.from_handle(runtime.quake))
  quakeHost:set_geometry(quake:get_geometry())
  if visible then show(0) else hide(0) end
end

return {
  hide = hide,
  show = show,
  toggle = toggle,
}

