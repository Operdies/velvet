local M = {}

--- @alias velvet.copy_mode
--- | 'none'
--- | 'yank'
--- | 'visual'
--- | 'lines'
--- | 'block'

--- @type table<velvet.copy_mode, velvet.copy_mode>
local modes = { none = 'none', visual = 'visual', lines = 'lines', block = 'block', yank = 'yank' }
local vk = require('velvet.keymap.named_keys')
local mo = require('velvet.copy_mode.motions')

--- @param from velvet.api.coordinate
--- @param to velvet.api.coordinate
--- @param id integer
--- @param mode velvet.copy_mode
local function get_selection_ranges(from, to, id, mode)
  local geom = vv.api.window_get_geometry(id)
  if mode == 'block' then
    local lcol, rcol = from.col, to.col
    local row1, row2 = from.row, to.row
    if lcol > rcol then lcol, rcol = rcol, lcol end
    if row1 > row2 then row1, row2 = row2, row1 end
    local ranges = {}
    for row = row1, row2 do
      if row > geom.height then break end
      ranges[#ranges + 1] = { row = row, lcol = lcol, rcol = rcol }
    end
    return ranges, geom
  else
    local start, _end = from, to
    if start.row > _end.row then start, _end = _end, start end
    local ranges = {}
    for row = start.row, _end.row do
      if row > geom.height then break end
      local lcol = mode ~= 'lines' and row == start.row and start.col or 1
      local rcol =  mode ~= 'lines' and row == _end.row and _end.col or geom.width
      if start.row == _end.row and lcol > rcol then lcol, rcol = rcol, lcol end
      ranges[#ranges + 1] = { row = row, lcol = lcol, rcol = rcol }
    end
    return ranges
  end
end

--- @param initial_mode? velvet.copy_mode
local function do_copy(initial_mode)
  local target = vv.api.get_focused_window()
  local initial_offset = vv.api.window_get_scroll_offset(target)
  local text_color = 'bright_black'
  local confirm_text_color = 'bright_black'
  local selection_highlight_color = 'magenta'
  local selection_confirm_color = 'yellow'
  local mode = modes.none
  local disposed = false
  local win = require('velvet.window')
  local km = require('velvet.keymap').create({ async = false })
  local overlay = win.create()
  local function dispose()
    if disposed then return end
    disposed = true
    if overlay and overlay:valid() then
      overlay:close()
    end
    if target and vv.api.window_is_valid(target) then
      vv.api.window_set_scroll_offset(target, initial_offset)
      vv.api.set_focused_window(target)
    end
  end
  vv.async.defer(dispose)

  local on_key = {
    event = 'window.on_key',
    when = function(_, e) return not km.is_modifier(e.data) and e.data.key.event_type ~= 'release' and e.data.win_id == overlay.id end
  }
  local either_closed = {
    event = 'window.closed',
    when = function(_, e) return e.data.win_id == target or e.data.win_id == overlay.id end
  }

  local target_geometry = vv.api.window_get_geometry(target)
  local cursor = vv.api.window_get_cursor_position(target)
  local start_selection, end_selection

  overlay:set_geometry(target_geometry)
  overlay:set_z_index(vv.z_hint.overlay)
  -- the overlay should be completely translucent except for highlighted text
  overlay:set_background_color('#00000000')
  overlay:set_foreground_color(text_color)
  overlay:clear()
  overlay:focus()
  overlay:set_cursor(cursor.col, cursor.row)

  local function cursor_move_x(dx)
    overlay:set_cursor(cursor.col + dx, cursor.row)
    cursor = overlay:get_cursor()
  end

  local function scroll(dy)
    local cur = vv.api.window_get_scroll_offset(target)
    vv.api.window_set_scroll_offset(target, cur - dy)
  end

  local function cursor_move_y(dy)
    if dy < 0 then
      local cursor_delta = math.min(cursor.row - 1, -dy)
      dy = dy + cursor_delta
      cursor.row = cursor.row - cursor_delta
      if dy < 0 then scroll(dy) end
      overlay:set_cursor(cursor.col, cursor.row)
    else
      local cursor_delta = math.min(target_geometry.height - cursor.row, dy)
      dy = dy - cursor_delta
      cursor.row = cursor.row + cursor_delta
      if dy > 0 then scroll(dy) end
      overlay:set_cursor(cursor.col, cursor.row)
    end
  end

  local function get_abs_cursor()
    local offset = vv.api.window_get_scroll_offset(target)
    local cur = overlay:get_cursor()
    cur.row = cur.row - offset
    return cur
  end

  local function cursor_move(dx, dy)
    if dx ~= 0 then cursor_move_x(dx) end
    if dy ~= 0 then cursor_move_y(dy) end
    cursor = overlay:get_cursor()
    end_selection = get_abs_cursor()
  end

  local function selection_mode(new_mode)
    if mode == modes.none then
      start_selection = get_abs_cursor()
      end_selection = start_selection
    end
    mode = new_mode
  end

  local function apply(f, ...)
    local args = {...}
    return function() f(table.unpack(args)) end
  end

  --- @return velvet.api.line
  local function get_line(line, lcol, rcol)
      return vv.api.window_get_text(target, { top = line, height = 1, left = lcol, width = rcol - lcol + 1 })[1]
  end

  local function copy_and_dispose()
    if mode == modes.none then dispose(); return; end
    local ranges = get_selection_ranges(start_selection, end_selection, target, mode)
    local ll = {}
    if mode == 'block' then
      -- block copying does not need to consider line wraps.
      local r1 = ranges[1]
      local r2 = ranges[#ranges]
      local height = 1 + r2.row - r1.row
      local lines = vv.api.window_get_text(target,
        { top = r1.row, height = height, left = r1.lcol, width = r2.rcol - r1.lcol + 1 })
      for i, l in ipairs(lines) do
        ll[i] = l.text:match('(.-)%s*$')
      end
    else
      local wrapping = false
      for _, r in ipairs(ranges) do
        local line = get_line(r.row, r.lcol, r.rcol)
        local text = line.wraps and line.text or line.text:match('(.-)%s*$')
        local index = wrapping and #ll or #ll + 1
        if wrapping then text = ll[index] .. text end
        ll[index] = text
        wrapping = line.wraps
      end
      if #ll > 0 then ll[#ll] = ll[#ll]:match('(.-)%s*$') end
    end
    local text = table.concat(ll, '\n')
    vv.api.clipboard_set(text)
    vv.async.run(function()
      overlay:set_foreground_color(confirm_text_color)
      selection_highlight_color = selection_confirm_color
      vv.async.wait(150)
      dispose()
    end)
  end

  local digit = nil
  local function copy_or_yank()
    local cur = get_abs_cursor()
    if mode == 'none' then
      mode = 'yank'
      start_selection = { col = cur.col, row = cur.row }
      end_selection = start_selection
      return
    elseif mode == 'yank' then
      local count = digit or 1
      mode = 'lines'
      end_selection = { col = start_selection.col, row = math.min(cur.row + count - 1, target_geometry.height) }
    end
    copy_and_dispose()
  end

  local function set_abs_cursor(abs)
    local offset = vv.api.window_get_scroll_offset(target)
    local local_row = offset + abs.row
    if local_row < 1 then
      local to_scroll = local_row - 1
      scroll(to_scroll)
      local_row = local_row - to_scroll
    elseif local_row > target_geometry.height then
      local to_scroll = local_row - target_geometry.height
      scroll(to_scroll)
      local_row = local_row - to_scroll
    end
    overlay:set_cursor(abs.col, local_row)
  end

  local function other(o)
    if mode == modes.none then return end
    if o == modes.block and mode == modes.block then
      start_selection.col, end_selection.col = end_selection.col, start_selection.col
    else
      start_selection, end_selection = end_selection, start_selection
    end
    set_abs_cursor(end_selection)
  end

  local function pan(dy)
    local cur1 = overlay:get_cursor()
    local offset = vv.api.window_get_scroll_offset(target)
    scroll(dy)
    local delta = offset - vv.api.window_get_scroll_offset(target)
    if delta ~= 0 and dy < 0 and cur1.row < target_geometry.height then
      cursor_move(0, -dy)
    elseif delta ~= 0 and dy > 0 and cur1.row > 1 then
      cursor_move(0, -dy)
    end
    end_selection = get_abs_cursor()
  end

  --- @param motion velvet.copy.vim_motion
  --- @param count? integer
  local function vim_motion(motion, count)
    local op = nil
    local operand_required = { f = true, F = true, t = true, T = true, g = true, a = true, i = true }
    if operand_required[motion] then
      local r, operand = vv.async.wait(on_key, either_closed)
      if r == either_closed then
        dispose()
        return
      end
      op = operand.data.key.name
      -- space is treated as a named key, so we should convert its name to ' '
      if operand.data.key.codepoint == 32 then op = ' ' end
      -- don't attempt to handle named keys
      if op ~= ' ' and vk[op] then return end
    end
    local cur1 = get_abs_cursor()
    local _end, _start = mo.move(target, cur1, motion, count, op)
    if _start then
      -- TODO: this approach doesn't really work.
      -- It was intended to make e.g. vap select a paragraph by updating both
      -- ranges, but I hadn't considered that vap->ap->ap should extend
      -- the selection with an additional paragraph each timer.
      -- This hack works for that specific appraoch, but o->ap should extend
      -- the selection in the other direction, so we really need a more
      -- extensive visual mode emulation which can properly account
      -- for which end of the selection is active, and pass that information
      -- to the motion implementation so it knows which direction to extend in.
      if cur1.col == start_selection.col and cur1.row == start_selection.row then
        start_selection = _start
      end
    end
    set_abs_cursor(_end)
    end_selection = _end
    if mode == 'yank' then
      -- some motions implicitly select whole lines
      -- this is probably not exhaustive.
      local yank_motion_modes = {
        j = 'lines', k = 'lines', G = 'lines'
      }
      mode = yank_motion_modes[motion] or 'visual'
      if motion == 'g' then mode = op == 'g' and 'lines' or 'visual' end
      copy_and_dispose()
    end
  end


  -- TODO: 
  -- %: match closing symbol
  -- dwm transient modeline
  local keymap = {
    { { 'q', '<esc>' },                dispose },
    { { 'v' },                         apply(selection_mode, modes.visual) },
    { { '<C-v>' },                     apply(selection_mode, modes.block) },
    { { '<S-v>' },                     apply(selection_mode, modes.lines) },
    { { '<C-c>' },                     copy_and_dispose },
    { { 'y', },                        copy_or_yank },
    { { 'o' },                         other },
    { { 'O' },                         apply(other, modes.block) },
    { { '<C-y>' }, apply(pan, -1) },
    { { '<C-e>' }, apply(pan, 1) },
  }

  -- TODO: alias 'normal' mappings to vim motions
  local aliases = {
    k = { '<up>' },
    j = { '<down>' },
    h = { '<left>' },
    l = { '<right>' },
    ['$'] = { '<end>' },
    ['0'] = { '<home>' },
  }

  for _, m in ipairs(mo.motions) do
    local lst = aliases[m] or {}
    lst[#lst + 1] = m
    keymap[#keymap + 1] = { lst, function()
      local cnt = digit; digit = nil; vim_motion(m, cnt)
    end }
  end

  for i = 0, 9 do
    keymap[#keymap + 1] = { { tostring(i) }, function()
      if i == 0 and digit == nil then
        vim_motion('0')
      else
        digit = (digit or 0) * 10 + i
        if digit > 100000 then digit = 100000 end
      end
    end }
  end

  for _, mappings in ipairs(keymap) do
    for _, keys in ipairs(mappings[1]) do
      km:set(keys, mappings[2])
    end
  end

  local function draw()
    local c1 = overlay:get_cursor()
    overlay:set_background_color('#00000000')
    overlay:clear()
    if mode ~= modes.none then
      local row1 = start_selection.row < end_selection.row and start_selection.row or end_selection.row
      local ranges = get_selection_ranges(start_selection, end_selection, target, mode)
      overlay:set_background_color(selection_highlight_color)
      -- compute the visible portion of the selection
      local offset = vv.api.window_get_scroll_offset(target)
      local num_above = 1 - row1 - offset
      local num_below = #ranges - target_geometry.height - num_above
      local start = math.max(num_above, 1)
      local _end = math.min(#ranges, #ranges - num_below)
      for i = start, _end do
        local range = ranges[i]
        local line = vv.api.window_get_text(target, { top = range.row, height = 1, left = range.lcol, width = range.rcol - range.lcol + 1 })[1]
        local visual_row = row1 + (i-1) + offset
        overlay:set_cursor(range.lcol, visual_row)
        overlay:draw(line.text)
      end
    end
    overlay:set_cursor(c1.col, c1.row)
    cursor = c1
  end

  local focus_lost = {
    event = 'window.focus_changed',
    when = function(_, e) return e.data.new_focus ~= overlay.id end
  }

  local target_resized = { event = 'window.resized', when = function(_, evt) return evt.data.win_id == target end }
  -- target output can cause scrolling which must be handled
  local target_output = { event = 'window.output', when = function(_, evt) return evt.data.win_id == target end }

  if initial_mode then selection_mode(initial_mode) end
  draw()

  -- FIXME: this approach breaks when the scrollback buffer overflows.
  -- The C API must provide some mechanism for answering the question, "how many lines were added?",
  -- or a way to reference a fixed line.
  local sz = vv.api.window_get_scrollback_size(target)
  for reg, evt in vv.async.stream(on_key, either_closed, focus_lost, target_resized, target_output) do
    local sz2 = vv.api.window_get_scrollback_size(target)
    if sz ~= sz2 then
      local delta = sz2 - sz

      local off = vv.api.window_get_scroll_offset(target)
      if off == 0 then 
        cursor_move(0, -delta) 
      elseif end_selection then
        end_selection.row = end_selection.row - delta
      end

      if start_selection then
        start_selection.row = start_selection.row - delta
      end
      sz = sz2
    end
    if reg == on_key then
      km:on_key(evt.data)
      if tonumber(evt.data.key.name) == nil then digit = nil end
    elseif reg == focus_lost then
      dispose()
    elseif reg == either_closed then
      break
    elseif reg == target_resized then
      overlay:set_geometry(evt.data.new_size)
      target_geometry = evt.data.new_size
    end
    if disposed then break end
    local success, result = xpcall(draw, debug.traceback)
    if not success then printerr(result) end
  end
end

--- @param mode? velvet.copy_mode
function M.start(mode)
  if mode and not modes[mode] then
    local valid = {}; for k in pairs(modes) do valid[#valid + 1] = k end
    error(string.format("Bad argument #1: invalid mode. Expected one of %s", table.concat(valid, ", ")))
  end
  return vv.async.run(do_copy, mode)
end

return M

