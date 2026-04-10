#!/bin/sh
_=[[
exec vv lua "$0" "$@"
]]

--[[
-- This script demonstrates how overlays can be used to draw user attention.
--]]

local torch_render = vv.events.create_group('torch_group', true)
local torch = require('velvet.window').create()
torch:set_cursor_visible(false)
torch:set_alternate_screen(true)
torch:set_z_index(vv.z_hint.overlay)
torch:set_line_wrapping(false)
torch:set_auto_return(false)
torch:focus()

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
  torch:set_background_color('#000000ff')
  torch:clear()

  local function dist(c1, c2)
    local a = c1.col - c2.col
    local b = c1.row - c2.row
    return math.sqrt(a * a + (4 * b * b))
  end

  local span = 30
  local color = { red = 0, green = 0, blue = 0, alpha = 0 }
  for row = math.max(pos.row - span, 1), math.min(pos.row + span, sz.height) do
    local col1 = math.max(pos.col - span, 1)
    torch:set_cursor(col1, row)
    for col = col1, math.min(pos.col + span, sz.width) do
      local d = dist(pos, { col = col, row = row })
      color.alpha = d / span
      torch:set_background_color(color)
      torch:draw(' ')
    end
  end
end

local function invalidate()
  -- force invalidate to trigger the pre_render
  torch:draw(' ')
end

torch:on_mouse_move(invalidate)
torch:on_window_on_key(function(_, k)
  if k.key.name == 'ESCAPE' then
    vv.events.delete_group(torch_render)
    torch:close()
  end
end)
torch:on_screen_resized(invalidate)
-- only draw on pre-render because mouse events are extremely busy.
torch_render.pre_render = function() draw() end

