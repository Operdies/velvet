-- this quake implementation does not get invalidated for some reason.
-- let's figure it out!
do
  local quake_evt = vv.events.create_group("quake_resize_event", true)

  --- @type velvet.window
  local quakeHost = nil
  --- @type velvet.window
  local quake = nil

  local function get_size()
    local screen_size = vv.api.get_screen_geometry()
    local minwidth = 3
    local width = screen_size.width + 2
    if width < minwidth then width = minwidth end
    local height = screen_size.height // 3
    local minheight = math.min(10, screen_size.height)
    if height < minheight then height = minheight end
    local hostSize = { width = width, height = height, left = screen_size.width // 2 - (width // 2), top = 1 + screen_size.height - height }
    return hostSize
  end

  local function setsize()
    quake:set_frame_enabled(true)
    quake:set_frame_color('magenta')
    quake:set_z_index(99999)
    quake:set_title("─Quake")
    quake:set_opacity(0.7)
    quakeHost:set_background_color('black')
    quakeHost:set_opacity(0.1)
    quakeHost:set_z_index(quake:get_z_index())
    local hostSize = get_size()
    quakeHost:set_geometry(hostSize)
    quake:set_geometry({ width = hostSize.width - 2, height = hostSize.height - 2, left = hostSize.left + 1, top = hostSize
      .top + 1 })
    quakeHost:clear()
  end

  local function create_quake()
    quakeHost = velvet_window.create()
    quake = quakeHost:create_child_process_window("zsh", { working_directory = vv.api.window_get_working_directory(vv.api.get_focused_window()) })
    quake_evt.screen_resized = setsize

    quake:on_window_closed(function() quakeHost:close() end)
    quake:on_window_moved(function(_, args)
      local geom = args.new_size
      geom.height = geom.height + 2
      geom.width = geom.width + 2
      geom.left = geom.left - 1
      geom.top = geom.top - 1
      quakeHost:set_geometry(geom)
    end)
    setsize()
  end

  create_quake()

  local prevFocus = nil
  local visible = true

  local anim = require('velvet.stdlib.animation')

  local anim_duration = 150
  local function hide()
    if prevFocus and vv.api.window_is_valid(prevFocus) then
      vv.api.set_focused_window(prevFocus)
      prevFocus = nil
    end

    local screen = vv.api.get_screen_geometry()
    local new_size = get_size()
    new_size.top = screen.height
    quake:set_visibility(false)
    quakeHost:set_visibility(false)
    anim.animate(quake.id, new_size, anim_duration, { easing_function = anim.easing.spring })
    visible = false
  end

  local function show()
    prevFocus = vv.api.get_focused_window()
    quake:set_visibility(true)
    quakeHost:set_visibility(true)
    local new_size = get_size()
    anim.animate(quake.id, new_size, anim_duration, { 
      easing_function = anim.easing.spring,
      on_completed = function() quake:focus() end
    })
    visible = true
  end

  local function toggle()
    if not quake:valid() then
      if quakeHost and quakeHost:valid() then quakeHost:close() end
      create_quake()
    end
    if visible then hide() else show() end
  end

  map("<F1>", toggle)
end

