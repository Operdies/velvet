local vv = require('velvet')

--- @class velvet.animation
local animation = {}

local function round(x)
  if x >= 0 then
    return math.floor(x + 0.5)
  else
    return math.ceil(x - 0.5)
  end
end

animation.easing = {
  overshoot = function(t)
    local s = 1.70158
    return 1 + (((t - 1) * (t - 1)) * ((s + 1) * (t - 1) + s))
  end,
  linear = function(t) return t end,
  spring = function(t)
    local w = 8
    local x = 1.0 - (1.0 + w * t) * math.exp(-w * t)
    local norm = 1.0 - (1.0 + w) * math.exp(-w)
    return x / norm
  end,
}

local animating = {}

--- @class animate_options
--- @field easing_function function easing function
--- @field done function callback called when the animation completes

--- Change the dimensions of window |id| to |target| over |duration| ms
--- @param id integer Window ID
--- @param target velvet.api.window.geometry Final window geometry
--- @param duration integer Animation duration in milliseconds
--- @param opts? animate_options additional parameters
function animation.animate(id, target, duration, opts)
  if animating[id] then return end
  animating[id] = true

  local start_time = vv.api.get_current_tick()
  local geom = vv.api.window_get_geometry(id)
  local delta_x = target.left - geom.left
  local delta_y = target.top - geom.top
  local delta_w = target.width - geom.width
  local delta_h = target.height - geom.height

  local ease = opts and opts.easing_function or animation.easing.linear
  local f = function() end
  f = function()
    if not vv.api.window_is_valid(id) then
      animating[id] = nil
      return
    end
    local elapsed = vv.api.get_current_tick() - start_time
    if elapsed >= duration then
      animating[id] = nil
      vv.api.window_set_geometry(id, target)
      if opts and opts.done then opts.done() end
      return
    end
    local pct = ease(elapsed / duration)
    local frame_geom = {
      left = round(geom.left + delta_x * pct),
      top = round(geom.top + delta_y * pct),
      width = round(geom.width + delta_w * pct),
      height = round(geom.height + delta_h * pct),
    }
    vv.api.window_set_geometry(id, frame_geom)
    vv.api.schedule_after(5, f)
  end
  f()
end

return animation
