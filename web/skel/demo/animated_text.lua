#!/bin/sh
_=[[
exec vv lua "$0" "$@"
]]
--[[
-- This script demonstrates how windows can be unobtrusively scripted.
-- Try executing it by running `vv lua < demo/animated_text.lua
--]]

local winapi = require('velvet.window')

local function animated_typing()
  local poem = [[
Two roads diverged in a yellow wood,
And sorry I could not travel both
And be one traveler, long I stood
And looked down one as far as I could
To where it bent in the undergrowth;

Then took the other, as just as fair,
And having perhaps the better claim,
Because it was grassy and wanted wear;
Though as for that the passing there
Had worn them really about the same,

And both that morning equally lay
In leaves no step had trodden black.
Oh, I kept the first for another day!
Yet knowing how way leads on to way,
I doubted if I should ever come back.

I shall be telling this with a sigh
Somewhere ages and ages hence:
Two roads diverged in a wood, and I—
I took the one less traveled by,
And that has made all the difference.]]

  -- create a new lightweight windo -- this is manipulated in lua, and does not have a backing PTY like a normal process.
  local win = winapi.create()
  -- set up an event handler for this window -- this is just for handling resizing to anchor it to the top right corner.
  local evt = vv.events.create_group('animated_typing', true)

  local function dispose()
    vv.events.delete_group(evt)
    if win and win:valid() then win:close() end
  end

  local anchor = function()
    if not win:valid() then
      dispose(); return
    end
    local term_size = vv.api.get_screen_geometry()
    local width = math.min(38, term_size.width)
    local height = math.min(23, term_size.height)
    -- anchor to top right corner
    win:set_geometry({ width = width, height = height, top = 3, left = term_size.width - width - 2 })
    -- add window borders
    win:set_frame_enabled(true)
    win:set_frame_color('magenta')
    win:set_title("The Road Not Taken ── Robert Frost")
    -- draw the window above most other windows
    win:set_z_index(vv.z_hint.popup)
  end

  evt.screen_resized = anchor
  anchor()

  local pos = 1
  while pos <= #poem and win:valid() do
    -- grab next chunk: a run of spaces, a newline, or a word
    local s, e = poem:find("[ ]+", pos)
    local delay = 0
    if s ~= pos then
      s, e = poem:find("\n", pos)
      delay = 800
    end
    if s ~= pos then
      s, e = poem:find("[%w]+", pos)
      delay = e and s and (e - s) * 30 or 0
    end
    if s ~= pos then
      s, e = poem:find("[%p]+", pos)
      delay = 500
    end
    if s ~= pos then
      s, e = poem:find(".", pos)
      delay = 500
    end
    if not s or s ~= pos then return end

    local chunk = poem:sub(s, e)
    pos = e + 1

    win:draw(chunk)

    if chunk:find("\n") then
      delay = 500
    elseif chunk:find("[%p%-]") then
      delay = 300
    elseif chunk:find("^[ ]+$") then
      delay = 0
    end
    vv.async.wait(delay)
  end
  vv.async.wait(5000)
  dispose()
end

animated_typing()
