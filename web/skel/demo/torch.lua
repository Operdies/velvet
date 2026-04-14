#!/bin/sh
_=[[
exec vv lua "$0" "$@"
]]

--[[
-- This script demonstrates how overlays can be used to draw user attention.
--]]

local torch = require('velvet.window').create()
torch:set_cursor_visible(false)
torch:set_alternate_screen(true)
torch:set_z_index(vv.z_hint.overlay)
torch:set_line_wrapping(false)
torch:set_auto_return(false)

local function clamp(x, l, h)
  if x < l then return l end
  if x > h then return h end
  return x
end

local function draw()
  local pos = vv.api.get_mouse_position()
  local sz = vv.api.get_screen_geometry()
  pos.col = clamp(pos.col, 1, sz.width)
  pos.row = clamp(pos.row, 1, sz.height)
  torch:set_geometry({left = 1, top = 1, width = sz.width, height = sz.height })
  local max_alpha = 0.9
  local color = { red = 0, green = 0, blue = 0, alpha = max_alpha }
  torch:set_background_color(color)
  torch:clear()

  local function dist(c1, c2)
    local a = c1.col - c2.col
    local b = c1.row - c2.row
    return math.sqrt(a * a + b * b)
  end

  local rx = 30
  -- the exact ratio varies, but 16/34 is the ratio between cell width/height in my setup.
  local ry = math.floor(rx * (16 / 34))
  local norm_pos = { col = pos.col / rx, row = pos.row / ry }

  for row = math.max(pos.row - ry, 1), math.min(pos.row + ry, sz.height) do
    local col1 = math.max(pos.col - rx, 1)
    torch:set_cursor(col1, row)
    for col = col1, math.min(pos.col + rx, sz.width) do
      local d = dist(norm_pos, { col = col / rx, row = row / ry })
      color.alpha = math.min(d, max_alpha)
      torch:set_background_color(color)
      torch:draw(' ')
    end
  end
end

-- pass all overlay mouse events through to the window below
local function pass() return 'passthrough' end
torch:on_mouse_click(pass)
torch:on_mouse_move(pass)
torch:on_mouse_scroll(pass)

vv.async.defer(function() torch:close() end)
draw()
while true do
  local _, event = vv.async.wait('mouse.move', 'session.on_key', 'window.closed', 'screen.resized')
  -- since async.wait() yields, the window could have been closed during the wait() call
  if not torch:valid() then break end
  if event.name == 'mouse.move' or event.name == 'screen.resized' then
    -- force invalidate to trigger the pre_render
    torch:draw(' ')
    -- defer redrawing the screen until velvet schedules a render.
    -- otherwise we waste time creating intermediate frames which will never be presented.
    vv.async.wait_for_pre_render()
    if not torch:valid() then break end
    draw()
  elseif event.name == 'session.on_key' then
    if event.data.key.name == 'ESCAPE' then torch:close(); break; end
  end
end
