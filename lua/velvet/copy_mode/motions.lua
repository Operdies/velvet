--- @alias velvet.copy.vim_motion 'a'|'i'|'b'|'B'|'e'|'E'|'w'|'W'|'{'|'}'|'f'|'F'|'t'|'T'|'h'|'j'|'k'|'l'|'$'|'0'|'^'|'g'|'G' |'<C-d>'| '<C-f>'| '<C-b>'| '<C-u>'| '<C-e>'| '<C-y>' | ';' | ',' | '%'

--- @type velvet.api.rect
local geom = nil -- for convenience, always initialized to the current window size
--- @type velvet.api.cell_line
local line1 = nil -- for convenience, always initialized with the current cursor line

--- @type integer
local first_line = nil
--- @type integer
local last_line = nil

local punctuation = [[!"#$%&'()*+,-./:;<=>?@[\]^`{|}~]]
local whitespace = [[ ]]
local function make_class(chars)
  return "[" .. chars
      :gsub("%%", "%%%%")
      :gsub("%]", "%%]")
      :gsub("%-", "%%-")
      .. "]"
end

local classes = {
  -- punctuation
  pt = make_class(punctuation),
  -- whitespace
  ws = make_class(whitespace),
}

--- @param str? string
--- @return 'punctuation'|'whitespace'|'word'|'continuation'
local function classify(str)
  if not str then return 'continuation' end
  if str:match(classes.pt) then return 'punctuation' end
  if str:match(classes.ws) then return 'whitespace' end
  return 'word'
end

--- @param id integer
--- @param col integer
--- @param row integer
--- @param match fun(c: velvet.api.cell, col: integer, row: integer): boolean
--- @return integer col, integer row
local function scan_backward(id, col, row, match)
  local line = vv.api.window_get_cells(id, { left = 1, top = row, width = geom.width, height = 1 })[1]
  while row >= first_line do
    local cells = line.cells
    col = math.min(col, #cells)
    while col >= 1 do
      if match(cells[col], col, row) then return col, row end
      col = col - 1
    end
    row = row - 1
    if row >= first_line then
      line = vv.api.window_get_cells(id, { left = 1, top = row, width = geom.width, height = 1 })[1]
      col = #line.cells
    end
  end
  return math.max(col, 1), row
end


--- @param id integer
--- @param col integer
--- @param row integer
--- @param match fun(c: velvet.api.cell, col: integer, row: integer): boolean
--- @return integer col, integer row
local function scan_forward(id, col, row, match)
  while row <= geom.height do
    local line = vv.api.window_get_cells(id, { left = 1, top = row, width = geom.width, height = 1 })[1]
    local cells = line.cells
    while col <= #cells do
      if match(cells[col], col, row) then return col, row end
      col = col + 1
    end
    row = row + 1
    col = 1
  end
  return col, row
end

--- @param id integer
--- @param col integer
--- @param row integer
--- @return { col: integer, row: integer }
local function skip_whitespace(id, col, row)
  col, row = scan_forward(id, col, row, function(c) return c.content ~= ' ' end)
  return { col = col, row = row }
end

--- @param id integer
--- @param col integer
--- @param row integer
--- @return { col: integer, row: integer }
local function skip_whitespace_backward(id, col, row)
  col, row = scan_backward(id, col, row, function(c) return c.content ~= ' ' end)
  return { col = col, row = row }
end

--- @param id integer
--- @param cur velvet.api.coordinate
--- @return velvet.api.coordinate
local function motion_w(id, cur)
  local cls = nil
  local col, row = scan_forward(id, cur.col, cur.row, function(cell, c, r)
    if not cell.content then return false end
    if r ~= cur.row then return true end
    if not cls then
      cls = classify(cell.content)
      return false
    end
    return r ~= cur.row or classify(cell.content) ~= cls
  end)

  return skip_whitespace(id, col, row)
end

--- @param id integer
--- @param cur velvet.api.coordinate
--- @return velvet.api.coordinate
local function motion_W(id, cur)
  local is_space = nil
  local col, row = scan_forward(id, cur.col, cur.row, function(cell, _, r)
    if not cell.content then return false end
    if r ~= cur.row then return true end
    if not is_space then
      is_space = cell.content == ' '
      return false
    end
    return r ~= cur.row or is_space and cell.content ~= ' '
  end)
  return skip_whitespace(id, col, row)
end

local function prev_cell(id, cur)
  local col, row = cur.col, cur.row
  if col > 1 then
    col = col - 1
  elseif row > first_line then
    row = row - 1
    local l = vv.api.window_get_cells(id, { left = 1, top = row, width = geom.width, height = 1 })[1]
    col = #l.cells
  end
  return { col = col, row = row }
end

local function next_cell(cur)
  local col, row = cur.col, cur.row
  if col < geom.width then return { col = col + 1, row = row } end
  if row < last_line then return { col = 1, row = row + 1 } end
  return { col = col, row = row }
end

--- @param id integer
--- @param cur velvet.api.coordinate
--- @return velvet.api.coordinate
local function motion_e(id, cur)
  cur = next_cell(cur)
  cur = skip_whitespace(id, cur.col, cur.row)
  local cls = nil
  local pc, pr = cur.col, cur.row
  scan_forward(id, cur.col, cur.row, function(cell, c, r)
    if not cell.content then return false end
    if r ~= cur.row then return true end
    if not cls then
      cls = classify(cell.content)
      return false
    end
    local is_match = r ~= cur.row or classify(cell.content) ~= cls
    if not is_match then
      pc, pr = c, r
    end
    return is_match
  end)
  return { col = pc, row = pr }
end

--- @param id integer
--- @param cur velvet.api.coordinate
--- @return velvet.api.coordinate
local function motion_b(id, cur)
  cur = prev_cell(id, cur)
  local col, row = cur.col, cur.row
  -- skip whitespace
  col, row = scan_backward(id, col, row, function(cell) return cell.content ~= ' ' end)
  local cls = nil
  scan_backward(id, col, row, function(cell, c, r)
    if not cell.content then return false end
    if r ~= cur.row then return true end
    if not cls then
      cls = classify(cell.content)
      return false
    end
    local is_match = r ~= cur.row or classify(cell.content) ~= cls
    if not is_match then
      col, row = c, r
    end
    return is_match
  end)
  return { col = col, row = row }
end

--- @param id integer
--- @param cur velvet.api.coordinate
--- @return velvet.api.coordinate
local function motion_B(id, cur)
  cur = prev_cell(id, cur)
  local col, row = cur.col, cur.row
  col, row = scan_backward(id, col, row, function(cell) return cell.content ~= ' ' end)
  scan_backward(id, col, row, function(cell, c, r)
    if cell.content == ' ' then return true end
    col, row = c, r
    return false
  end)
  return { col = col, row = row }
end

--- @param id integer
--- @param cur velvet.api.coordinate
--- @return velvet.api.coordinate
local function motion_ge(id, cur)
  local col, row = cur.col, cur.row
  local cls = nil
  col, row = scan_backward(id, col, row, function(cell, _, r)
    if not cell.content then return false end
    if r ~= cur.row then return true end
    if not cls then
      cls = classify(cell.content)
      return false
    end
    local is_match = r ~= cur.row or classify(cell.content) ~= cls
    return is_match
  end)
  col, row = scan_backward(id, col, row, function(cell) return cell.content ~= ' ' end)
  return { col = col, row = row }
end

--- @param id integer
--- @param cur velvet.api.coordinate
--- @return velvet.api.coordinate
local function motion_E(id, cur)
  cur = next_cell(cur)
  cur = skip_whitespace(id, cur.col, cur.row)
  local pc, pr = cur.col, cur.row
  scan_forward(id, cur.col, cur.row, function(cell, c, r)
    local is_match = cell.content == ' ' or r ~= cur.row
    if not is_match then
      pc, pr = c, r
    end
    return is_match
  end)
  return { col = pc, row = pr }
end

--- @param id integer
--- @param cur velvet.api.coordinate
--- @param count? integer
--- @param aux? string
--- @return velvet.api.coordinate
local function motion_g(id, cur, count, aux)
  count = count or 1
  if aux == 'g' then
    local row = math.min(first_line + count - 1, last_line)
    local l = vv.api.window_get_cells(id, { top = row, left = 1, width = geom.width, height = 1 })[1]
    local col = math.min(cur.col, #l.cells)
    return { col = col, row = row }
  elseif aux == 'e' then
    for _ = 1, count do
      local cur1 = cur
      cur = motion_ge(id, cur)
      if cur1.col == cur.col and cur1.row == cur.row then break end
    end
    return cur
  end
  return cur
end

--- @param id integer
--- @param cur velvet.api.coordinate
--- @return velvet.api.coordinate
local function motion_G(id, cur, count)
  return motion_g(id, cur, count or 1 + geom.height - first_line, 'g')
end

local function row_empty(id, row)
  local text = vv.api.window_get_text(id, { top = row, left = 1, width = geom.width, height = 1 })[1].text
  return text:match('(.-)%s*$') == ''
end

local function row_width(id, row, trim)
  local cells = vv.api.window_get_cells(id, { top = row, left = 1, width = geom.width, height = 1 })[1].cells
  local w = #cells
  while trim and w > 1 and cells[w].content == ' ' do w = w - 1 end
  return w
end

local function find_char_forwards(id, cur, count, ch)
  count = count or 1
  local cur2 = next_cell(cur)
  local col, row = cur2.col, cur2.row
  local found = 0
  col, row = scan_forward(id, col, row, function(cell, c, r)
    if r ~= cur.row then return true end
    if cell.content == ch then
      found = found + 1
      return found == count
    end
    return false
  end)
  return (found == count and { col = col, row = row } or cur), found
end

local function find_char_backwards(id, cur, count, ch)
  count = count or 1
  local cur2 = prev_cell(id, cur)
  local col, row = cur2.col, cur2.row
  local found = 0
  col, row = scan_backward(id, col, row, function(cell, c, r)
    if r ~= cur.row then return true end
    if cell.content == ch then
      found = found + 1
      return found == count
    end
    return false
  end)
  return (found == count and { col = col, row = row } or cur), found
end

local function motion_F(id, cur, count, ch)
  local cur2, found = find_char_backwards(id, cur, count, ch)
  return found and cur2 or cur
end

local function motion_f(id, cur, count, ch)
  local cur2, found = find_char_forwards(id, cur, count, ch)
  return found and cur2 or cur
end

local function motion_t(id, cur, count, ch)
  local cur2, found = find_char_forwards(id, cur, count, ch)
  return found and prev_cell(id, cur2) or cur
end
local function motion_T(id, cur, count, ch)
  local cur2, found = find_char_backwards(id, cur, count, ch)
  return found and next_cell(cur2) or cur
end

local inverse_tbl = {
  f = 'F', t = 'T', F = 'f', T = 't',
}
local prev_search = nil

--- @type table<velvet.copy.vim_motion, fun(id: integer, cur: velvet.api.coordinate, count?: integer, aux?: any): velvet.api.coordinate, velvet.api.coordinate? >
local motion_table = {
  E = motion_E,
  W = motion_W,
  b = motion_b,
  B = motion_B,
  g = motion_g,
  G = motion_G,
  ['$'] = function(_, cur) return { row = cur.row, col = geom.width } end,
  ['0'] = function(_, cur) return { row = cur.row, col = 1 } end,
  ['^'] = function(_, cur)
    local col = 1
    local cs = line1.cells
    while col <= #cs and cs[col].content == ' ' do col = col + 1 end
    return { row = cur.row, col = col <= #line1.cells and col or 1 }
  end,
  e = motion_e,
  h = function(_, cur) return { row = cur.row, col = math.max(1, cur.col - 1) } end,
  j = function(_, cur) return { col = cur.col, row = math.min(last_line, cur.row + 1) } end,
  k = function(_, cur) return { col = cur.col, row = math.max(first_line, cur.row - 1) } end,
  l = function(_, cur) return { row = cur.row, col = math.min(cur.col + 1, geom.width) } end,
  w = motion_w,
  ['}'] = function(id, cur)
    local row = cur.row + 1
    while row <= last_line and row_empty(id, row) do row = row + 1 end
    while row <= last_line and not row_empty(id, row) do row = row + 1 end
    row = math.min(row, last_line)
    return { row = math.min(row, last_line), col = row_width(id, row, true) }
  end,
  ['{'] = function(id, cur)
    local row = cur.row - 1
    while row >= first_line and row_empty(id, row) do row = row - 1 end
    while row >= first_line and not row_empty(id, row) do row = row - 1 end
    return { row = math.min(row, last_line), col = 1 }
  end,
  ['<C-d>'] = function(_, cur)
    return { col = cur.col, row = math.min(cur.row + geom.height // 2, last_line) }
  end,
  ['<C-f>'] = function(_, cur)
    return { col = cur.col, row = math.min(cur.row + geom.height, last_line) }
  end,
  ['<C-b>'] = function(_, cur)
    return { col = cur.col, row = math.max(cur.row - geom.height, first_line) }
  end,
  ['<C-u>'] = function(_, cur)
    return { col = cur.col, row = math.max(cur.row - geom.height // 2, first_line) }
  end,
  f = motion_f,
  t = motion_t,
  F = motion_F,
  T = motion_T,
  ['%'] = function(id, cur)
    local delims = { ['{'] = '}', ['['] = ']', ['<'] = '>', ['('] = ')' }
    local reverse = {}; for k, v in pairs(delims) do reverse[v] = k end
    local open = nil
    local close = nil

    local col, row = scan_forward(id, cur.col, cur.row, function(c, _, r)
      if r ~= cur.row then return true end
      if reverse[c.content] then close = c.content end
      if delims[c.content] then open = c.content end
      return (open or close) ~= nil
    end)

    if open == nil and close == nil then return cur end

    local match_start = { col = col, row = row }

    local count = 0
    local scanner = nil
    if open then
      close = delims[open]
      count = 1
      local c1 = next_cell(match_start)
      col, row = c1.col, c1.row
      scanner = scan_forward
    else
      open = reverse[close]
      count = -1
      local c1 = prev_cell(id, match_start)
      col, row = c1.col, c1.row
      scanner = scan_backward
    end

    col, row = scanner(id, col, row, function(c)
      if c.content == open then count = count + 1 end
      if c.content == close then count = count - 1 end
      return count == 0
    end)

    if count == 0 then return { col = col, row = row } end
    return cur
  end
}

motion_table[','] = function(id, cur, count)
  if not prev_search then return cur end
  local motion = inverse_tbl[prev_search.motion]
  return motion_table[motion](id, cur, count, prev_search.char)
end

motion_table[';'] = function(id, cur, count)
  if not prev_search then return cur end
  return motion_table[prev_search.motion](id, cur, count, prev_search.char)
end

--- @return velvet.api.coordinate, velvet.api.coordinate
local function motion_ix(id, cur, count, motion)
  local pairs = {
    w = { 'b', 'e' }, W = { 'B', 'E' }, p = {'{', '}'}
  }
  local pair = pairs[motion]
  if not pair then
    error(string.format("Unsupported motion 'a%s'", motion))
  end

  local s, e = pair[1], pair[2]
  local cur_end = motion_table[e](id, cur)
  local cur_start = motion_table[s](id, cur_end)
  for _ = 2, (count or 1) do
    cur_end = motion_table[e](id, cur_end)
  end
  cur_start = skip_whitespace(id, cur_start.col, cur_start.row)
  cur_end = skip_whitespace_backward(id, cur_end.col, cur_end.row)
  return cur_end, cur_start
end

motion_table['i'] = motion_ix
motion_table['a'] = motion_ix

--- @param id integer window id
--- @param cursor velvet.api.coordinate initial cursor position
--- @param motion velvet.copy.vim_motion motion
--- @param count? integer number of repetitions (default 1)
--- @param arg1? any optional parameter, such as a string for 'f'|'F'|'t'|'T' (default nil)
--- @return velvet.api.coordinate coordinate, velvet.api.coordinate? start the cursor position after the motion, and optionally a match start position
local function move(id, cursor, motion, count, arg1)
  geom = vv.api.window_get_geometry(id)
  line1 = vv.api.window_get_cells(id, { left = 1, top = cursor.row, width = geom.width, height = 1 })[1]
  first_line = -(vv.api.window_get_scrollback_size(id) - 1)
  last_line = geom.height

  if not motion_table[motion] then printerr(string.format("Motion %s not implemented.", motion)); return cursor; end

  if inverse_tbl[motion] then
    prev_search = { motion = motion, char = arg1 }
  end

  -- for motions which require special count handling,
  -- pass the count directly to the motion instead of repeating it.
  local counted = { g = true, G = true, f = true, F = true, t = true, T = true, a = true, i = true }
  local fn = motion_table[motion]
  local rep = (counted[motion] and 1) or count or 1
  local match_start = nil
  for _ = 1, rep do
    local c1, r1 = cursor.col, cursor.row
    cursor, match_start = fn(id, cursor, count, arg1)
    if cursor.col == c1 and cursor.row == r1 then break end
    if cursor.row ~= r1 then
      line1 = vv.api.window_get_cells(id, { left = 1, top = cursor.row, width = geom.width, height = 1 })[1]
    end
  end
  return cursor, match_start
end

local motions = {
  --- @type velvet.copy.vim_motion[]
  motions = {
    '%',                          -- match closing symbol
    'b', 'B', 'e', 'E', 'w', 'W', -- word motions
    'a', 'i',                     -- around / inside
    '{', '}',                     -- paragraphs
    'f', 'F', 't', 'T',           -- search
    'h', 'j', 'k', 'l',           -- cursor movement
    '$', '0', '^',                -- line-wise
    'g', 'G',                     -- absolute
    '<C-d>', '<C-u>',             -- half screen scroll
    '<C-f>', '<C-b>',             -- whole screen scroll
    ';', ','                      -- repeat search forward/backward
    -- maybe do this later -- copy_mode already implements window scrolling
    -- '<C-e>', '<C-y>'              -- scroll window
  },
  move = move,
}

return motions
