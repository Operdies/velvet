#!/bin/sh
_=[[
exec vv lua "$0" "$@"
]]

--[[
-- Fun little clock wallpaper. It takes advantage of velvet's compositor transparency feature
-- to draw underneath text by setting the background color and drawing with spaces.
--]]


-- brick font stolen from: https://github.com/race604/clock-tui/blob/master/clock-tui/src/clock_text/font/bricks.rs
local font = {
  ["0"] = { { 0, 6 }, { 0, 2, 2, 2 }, { 0, 2, 2, 2 }, { 0, 2, 2, 2 }, { 0, 6 } },
  ["1"] = { { 0, 4 }, { 2, 2 }, { 2, 2 }, { 2, 2 }, { 0, 6 } },
  ["2"] = { { 0, 6 }, { 4, 2 }, { 0, 6 }, { 0, 2 }, { 0, 6 } },
  ["3"] = { { 0, 6 }, { 4, 2 }, { 1, 5 }, { 4, 2 }, { 0, 6 } },
  ["4"] = { { 0, 2, 2, 2 }, { 0, 2, 2, 2 }, { 0, 6 }, { 4, 2 }, { 4, 2 } },
  ["5"] = { { 0, 6 }, { 0, 2 }, { 0, 6 }, { 4, 2 }, { 0, 6 } },
  ["6"] = { { 0, 6 }, { 0, 2 }, { 0, 6 }, { 0, 2, 2, 2 }, { 0, 6 } },
  ["7"] = { { 0, 6 }, { 4, 2 }, { 4, 2 }, { 4, 2 }, { 4, 2 } },
  ["8"] = { { 0, 6 }, { 0, 2, 2, 2 }, { 0, 6 }, { 0, 2, 2, 2 }, { 0, 6 } },
  ["9"] = { { 0, 6 }, { 0, 2, 2, 2 }, { 0, 6 }, { 4, 2 }, { 0, 6 } },
  [":"] = { {}, { 2, 2 }, {}, { 2, 2 }, {} },
  ["."] = { {}, {}, {}, {}, { 2, 2 } },
  ["-"] = { {}, {}, { 0, 6 }, {}, {} },
  ["A"] = { { 0, 6 }, { 0, 2, 2, 2 }, { 0, 6 }, { 0, 2, 2, 2 }, { 0, 2, 2, 2 } },
  ["B"] = { { 0, 5 }, { 0, 2, 2, 2 }, { 0, 5 }, { 0, 2, 2, 2 }, { 0, 5 } },
  ["C"] = { { 0, 6 }, { 0, 2 }, { 0, 2 }, { 0, 2 }, { 0, 6 } },
  ["D"] = { { 0, 5 }, { 0, 2, 2, 2 }, { 0, 2, 2, 2 }, { 0, 2, 2, 2 }, { 0, 5 } },
  ["E"] = { { 0, 6 }, { 0, 2 }, { 0, 5 }, { 0, 2 }, { 0, 6 } },
  ["F"] = { { 0, 6 }, { 0, 2 }, { 0, 5 }, { 0, 2 }, { 0, 2 } },
}

local timer = require('velvet.window').create()
local function update()
  local text = os.date('%H:%M:%S')
  print(text)
  --- @cast text string
  local width = #text * 8
  local height = 7
  local sz = vv.api.get_screen_geometry()
  timer:set_geometry({ left = sz.width // 2 - width // 2, top = sz.height - height - 5, width = width, height = height })

  timer:clear_background_color()
  timer:clear()
  timer:set_alpha(0)
  timer:set_transparency_mode('clear')
  timer:set_background_color('red')
  timer:set_z_index(vv.z_hint.background + 1)
  timer:set_cursor_visible(false)
  local index = 0
  for chr in text:gmatch('.') do
    for i, segments in ipairs(font[chr]) do
      timer:set_cursor(3 + 8 * index, i + 1)
      for x, len in ipairs(segments) do
        if x % 2 == 0 then
          local draw = string.rep(' ', len)
          timer:draw(draw)
        elseif len > 0 then
          timer:draw(('\x1b[%dC'):format(len))
        end
      end
    end
    index = index + 1
  end
end

local function ticker()
  -- remove clock if coroutine is disposed
  vv.async.defer(function() timer:close() end)
  while timer:valid() do
    update()
    vv.async.wait('screen.resized', 1000)
  end
end
ticker()

