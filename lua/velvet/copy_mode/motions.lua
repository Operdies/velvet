--- @alias velvet.copy.vim_motion 'a'|'i'|'b'|'B'|'e'|'E'|'w'|'W'|'{'|'}'|'f'|'F'|'t'|'T'|'h'|'j'|'k'|'l'|'$'|'0'|'^'|'g'|'G' |'<C-d>'| '<C-f>'| '<C-b>'| '<C-u>'| '<C-e>'| '<C-y>' | ';' | ',' | '%'

--- @type velvet.api.rect
local geom = nil  -- for convenience, always initialized to the current window size
--- @type velvet.api.cell_line
local line1 = nil -- for convenience, always initialized with the current cursor line

--- @type velvet.api.cell_line[]
local linebuf = {}

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

--- @return velvet.api.cell_line?
local function get_line(id, row)
  if row < first_line or row > last_line then return nil end
  if linebuf[row] then return linebuf[row] end
  local ll = vv.api.window_get_cells(id, { left = 1, top = row, width = geom.width, height = 1 })[1]
  linebuf[row] = ll
  return ll
end

--- @param cur velvet.api.coordinate
--- @return velvet.api.cell?
local function get_cell(id, cur)
  local line = get_line(id, cur.row)
  return line and line.cells[cur.col]
end

local function row_empty(id, row)
  local text = vv.api.window_get_text(id, { top = row, left = 1, width = geom.width, height = 1 })[1].text
  return text:match('(.-)%s*$') == ''
end

local function row_width(id, row, trim)
  local cells = assert(get_line(id, row)).cells
  local w = #cells
  while trim and w > 1 and cells[w].content == ' ' do w = w - 1 end
  return w
end

local function cur_prev(id, cur)
  local col, row = cur.col, cur.row
  if col > 1 then
    col = col - 1
  elseif row > first_line then
    row = row - 1
    local l = assert(get_line(id, row))
    col = #l.cells
  end
  return { col = col, row = row }
end

local function cur_next(cur)
  local col, row = cur.col, cur.row
  if col < geom.width then return { col = col + 1, row = row } end
  if row < last_line then return { col = 1, row = row + 1 } end
  return { col = col, row = row }
end

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
--- @param match fun(c: velvet.api.cell, col: integer, row: integer, peek?: velvet.api.cell): boolean
--- @return integer col, integer row
local function scan_backward(id, col, row, match)
  local line = assert(get_line(id, row))
  while row >= first_line do
    local cells = line.cells
    col = math.min(col, #cells)
    while col >= 1 do
      local peek = nil
      if col > 1 and cells[col - 1] then
        peek = cells[col - 1]
      elseif row - 1 > first_line then
        peek = get_cell(id, { row = row - 1, col = row_width(id, row - 1, false) })
      end
      if match(cells[col], col, row, peek) then return col, row end
      col = col - 1
    end
    row = row - 1
    if row >= first_line then
      line = assert(get_line(id, row))
      col = #line.cells
    end
  end
  return math.max(col, 1), row
end


--- @param id integer
--- @param col integer
--- @param row integer
--- @param match fun(c: velvet.api.cell, col: integer, row: integer, peek?: velvet.api.cell): boolean
--- @return integer col, integer row
local function scan_forward(id, col, row, match)
  while row <= geom.height do
    local line = assert(get_line(id, row))
    local cells = line.cells
    while col <= #cells do
      local peek = col < #cells and cells[col + 1] or get_cell(id, { col = 1, row = row + 1 })
      if match(cells[col], col, row, peek) then return col, row end
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


--- @param id integer
--- @param cur velvet.api.coordinate
--- @return velvet.api.coordinate
local function motion_e(id, cur)
  cur = cur_next(cur)
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
  cur = cur_prev(id, cur)
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
  cur = cur_prev(id, cur)
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
  cur = cur_next(cur)
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
    local l = assert(get_line(id, row))
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

local function find_char_forwards(id, cur, count, ch)
  count = count or 1
  local cur2 = cur_next(cur)
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
  local cur2 = cur_prev(id, cur)
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
  return found and cur_prev(id, cur2) or cur
end
local function motion_T(id, cur, count, ch)
  local cur2, found = find_char_backwards(id, cur, count, ch)
  return found and cur_next(cur2) or cur
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
      local c1 = cur_next(match_start)
      col, row = c1.col, c1.row
      scanner = scan_forward
    else
      open = reverse[close]
      count = -1
      local c1 = cur_prev(id, match_start)
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

local function seed(id, cursor)
  geom = vv.api.window_get_geometry(id)
  first_line = -(vv.api.window_get_scrollback_size(id) - 1)
  last_line = geom.height
  linebuf = {}
  line1 = assert(get_line(id, cursor.row))
end

--- @param id integer window id
--- @param cursor velvet.api.coordinate initial cursor position
--- @param motion velvet.copy.vim_motion motion
--- @param count? integer number of repetitions (default 1)
--- @param arg1? any optional parameter, such as a string for 'f'|'F'|'t'|'T' (default nil)
--- @return velvet.api.coordinate coordinate the cursor position after the motion
local function move(id, cursor, motion, count, arg1)
  seed(id, cursor)

  if not motion_table[motion] then
    printerr(string.format("Motion %s not implemented.", motion)); return cursor;
  end

  if inverse_tbl[motion] then
    prev_search = { motion = motion, char = arg1 }
  end

  -- for motions which require special count handling,
  -- pass the count directly to the motion instead of repeating it.
  local counted = { g = true, G = true, f = true, F = true, t = true, T = true, a = true, i = true }
  local fn = motion_table[motion]
  local rep = (counted[motion] and 1) or count or 1
  for _ = 1, rep do
    local c1, r1 = cursor.col, cursor.row
    cursor = fn(id, cursor, count, arg1)
    if cursor.col == c1 and cursor.row == r1 then break end
    if cursor.row ~= r1 then
      line1 = assert(get_line(id, cursor.row))
    end
  end
  return cursor
end

local function cur_eq(cur1, cur2) return cur1.col == cur2.col and cur1.row == cur2.row end
local function cur_lt(cur1, cur2)
  if cur1.row ~= cur2.row then return cur1.row < cur2.row end
  return cur1.col < cur2.col
end

--- @param state velvet.copy.selection_state
--- @return 'back'|'forward'|'both'
local function selection_direction(state)
  -- in line selection mode, don't consider columns for ordering purposes.
  if state.mode == 'lines' then
    return (state.start.row == state.cursor.row and 'both')
        or (state.start.row < state.cursor.row and 'forward') or 'back'
  end
  return (cur_eq(state.start, state.cursor) and 'both')
      or (cur_lt(state.start, state.cursor) and 'forward') or 'back'
end

--- @type table<string, fun(id: integer, state: velvet.copy.selection_state, count?: integer): velvet.copy.selection_state>
local textobject_dispatch = {}
textobject_dispatch.iw = function(id, state, count)
  count = count or 1
  state.mode = 'visual'
  for _ = 1, count do
    local c, s = state.cursor, state.start
    local c1, s1 = { col = c.col, row = c.row }, { col = s.col, row = s.row }
    local dir = selection_direction(state)
    local l = assert(get_line(id, state.cursor.row))
    local cls = l.cells[state.cursor.col] and classify(l.cells[state.cursor.col].content) or 'whitespace'
    local function same_class(_, _, _, peek) return peek == nil or classify(peek.content) ~= cls end
    if dir == 'both' then
      c.col, c.row = scan_forward(id, c.col, c.row, same_class)
      s.col, s.row = scan_backward(id, s.col, s.row, same_class)
    elseif dir == 'forward' then
      local start = cur_next(c)
      cls = classify(get_cell(id, start).content)
      c.col, c.row = scan_forward(id, start.col, start.row, same_class)
    else
      local start = cur_prev(id, c)
      cls = classify(get_cell(id, start).content)
      c.col, c.row = scan_backward(id, start.col, start.row, same_class)
    end
    if cur_eq(c, c1) and cur_eq(s, s1) then break end
  end
  return state
end

textobject_dispatch.aw = function(id, state, count)
  count = count or 1
  state.mode = 'visual'

  local function before_next_non_whitespace(scanner, cur)
    local col, row = scanner(id, cur.col, cur.row, function(_, _, _, p) return p and p.content ~= ' ' end)
    return { col = col, row = row }
  end

  local function consume_whitespace(both)
    local dir = selection_direction(state)
    if both and dir == 'both' then
      local cur1 = before_next_non_whitespace(scan_forward, state.cursor)
      if not cur_eq(state.cursor, cur1) then
        state.cursor = cur1
      else
        state.start = before_next_non_whitespace(scan_backward, state.start)
      end
    elseif dir == 'back' then
      state.cursor = before_next_non_whitespace(scan_backward, state.cursor)
    elseif dir == 'forward' then
      state.cursor = before_next_non_whitespace(scan_forward, state.cursor)
    end
  end

  for _ = 1, count do
    consume_whitespace(false)
    state = textobject_dispatch.iw(id, state, 1)
    consume_whitespace(true)
  end
  return state
end

--- @param id integer
--- @param delimiters string[][]
--- @param cur velvet.api.coordinate
--- @param count integer
--- @return boolean, velvet.api.coordinate, velvet.api.coordinate
local function select_block(id, delimiters, cur, count)
  local other_end = nil
  local open, close, counter = {}, {}, {}
  for _, p in ipairs(delimiters) do
    open[p[1]] = p[2]
    close[p[2]] = p[1]
    counter[p[1]] = count
  end

  do
    -- first try finding a match on the current line
    local function line_scan(scanner)
      local found = false
      local c, r = scanner(id, cur.col, cur.row, function(cell, _, r1)
        if r1 ~= cur.row then return true end
        found = open[cell.content]
        return found or close[cell.content]
      end)
      local match_start = { col = c, row = r }
      if found and match_start.row == cur.row then
        local match_end = motion_table['%'](id, match_start)
        if match_end.row == cur.row and not cur_eq(match_end, match_start) then
          return true, match_start, match_end
        end
      end
    end
    local success, match_start, match_end = line_scan(scan_backward)
    if not success then
      success, match_start, match_end = line_scan(scan_forward)
    end
    if success then return success, match_start, match_end end
  end

  local col, row = scan_backward(id, cur.col, cur.row, function(c, c1, r1)
    if c.content then
      if close[c.content] then
        counter[close[c.content]] = counter[close[c.content]] + 1
      elseif open[c.content] then
        counter[c.content] = counter[c.content] - 1
        if counter[c.content] == 0 then
          local cand = { col = c1, row = r1 }
          local match = motion_table['%'](id, cand)
          if not cur_eq(cand, match) then
            other_end = match
          end
        end
      end
    end
    return other_end ~= nil
  end)
  if other_end then return true, { col = col, row = row }, other_end end
  return false, {}, {}
end

local function ab_gen(delims)
  return function(id, state, count)
    local success, start, finish = select_block(id, delims, state.cursor, count or 1)
    if success then
      state.start = start
      state.cursor = finish
    end
    return state
  end
end

local function ib_gen(delims)
  return function(id, state, count)
    count = count or 1
    local success, start, finish = select_block(id, delims, state.cursor, count or 1)
    if success then
      state.start = cur_next(start)
      state.cursor = cur_prev(id, finish)
    end
    return state
  end
end

local block_delimiters = { { '{', '}' }, { '[', ']' }, { '(', ')' }, { '<', '>' } }
textobject_dispatch.ib = ib_gen(block_delimiters)
textobject_dispatch.ab = ab_gen(block_delimiters)
for _, pair in ipairs(block_delimiters) do
  for _, sym in ipairs(pair) do
    textobject_dispatch['i' .. sym] = ib_gen({pair})
    textobject_dispatch['a' .. sym] = ab_gen({pair})
  end
end

--- @class velvet.copy.selection_state
--- @field start velvet.api.coordinate
--- @field cursor velvet.api.coordinate
--- @field mode velvet.copy_mode

--- @param id integer window id
--- @param state velvet.copy.selection_state
--- @param selector velvet.copy.vim_text_selection selector
--- @param count? integer optional count
--- @return velvet.copy.selection_state new_state
local function textobject_select(id, state, selector, count)
  seed(id, state.cursor)
  if textobject_dispatch[selector] then return textobject_dispatch[selector](id, state, count) end
  printerr(string.format("selector %s not implemented.", selector))
  return state
end

--- @alias velvet.copy.vim_text_selection 'aw'|'iw'|'aW'|'iW'|'ap'|'ip'|'a]'|'a['|'i]'|'i['|'a)'|'a('|'i)'|'i('|'a}'|'a{'|'i}'|'i{'|'ab'|'ib'|'a"'|"a'"|'a`'|'i"'|"i'"|'i`'|'a>'|'a<'|'i>'|'i<'|'aq'|'iq' -- |'as'|'is'|'aB'|'iB'|'at'|'it'

local M = {
  --- @type velvet.copy.vim_motion[]
  motions = {
    '%',                          -- match closing symbol
    'b', 'B', 'e', 'E', 'w', 'W', -- word motions
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
  --- @type velvet.copy.vim_text_selection[]
  selectors = {
    'aw', 'aW', 'iw', 'iW',             -- words
    'ap', 'ip',                         -- paragraphs
    'a]', 'a[', 'a)', 'a(', 'a}', 'a{', -- a block
    'i]', 'i[', 'i)', 'i(', 'i}', 'i{', -- inner block
    'ab', 'ib',                         -- any block
    'a"', "a'", 'a`', 'i"', "i'", 'i`', -- a / inner quotes
    'aq', 'iq',                         -- a quote / inner quote. Non-standard extension, like ab/ib for quotes
    'a>', 'a<', 'i>', 'i<',             -- a / inner angle brackets
    -- 'as', 'is', -- a / inner sentence -- not implemented
    -- 'aB', 'iB', -- any escaped block -- not implemented
    -- 'at', 'it', -- a tag / inner tag - will not be implemented
  },
  move = move,
  select = textobject_select,
}

return M
