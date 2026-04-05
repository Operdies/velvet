local vv = require('velvet')

local win = require('velvet.window').create()
local bg = 'black'
win:set_z_index(vv.z_hint.background + 1)
win:set_alpha(0.1)
win:set_auto_return(true)
win:set_cursor_visible(false)
win:set_transparency_mode('all')
win:set_title('logpanel')

local arrange = function()
  local sz = vv.api.get_screen_geometry()
  local w = sz.width // 3
  if w < 50 then w = 50 end
  if w > sz.width then w = sz.width end
  local h = sz.height - 1
  win:set_geometry({ left = sz.width - w, top = 0, width = w, height = h })
  win:set_background_color(bg)
  -- win:clear()
  win:draw('\x1b[J')
end
arrange()

local events = require('velvet.events')
local ev = events.create_group('logpanel', true)
ev.screen_resized = arrange

ev.system_message = function(evt)
  local colors = {
    debug = 'blue',
    info = 'white',
    warning = 'yellow',
    error = 'red',
  }
  win:set_foreground_color(colors[evt.level] or colors.debug)
  win:set_background_color(bg)
  win:draw(evt.message)
  win:draw('\n')
end

win:set_visibility(false)

local function enable()
  win:set_visibility(true)
  vv.log('show log panel')
end

local function disable()
  win:set_visibility(false)
  vv.log('hide log panel')
end

return {
  enable = enable,
  disable = disable,
  toggle = function() if win:get_visibility() then disable() else enable() end end
}
