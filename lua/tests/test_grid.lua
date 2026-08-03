-- utility functions {{{1
local CSI = "\x1b["
-- CSI Ps A  Cursor Up Ps Times (default = 1) (CUU).
local function CUU(Ps) return CSI .. Ps .. "A" end
-- CSI Ps B  Cursor Down Ps Times (default = 1) (CUD).
local function CUD(Ps) return CSI .. Ps .. "B" end
-- CSI Ps C  Cursor Forward Ps Times (default = 1) (CUF).
local function CUF(Ps) return CSI .. Ps .. "C" end
-- CSI Ps D  Cursor Backward Ps Times (default = 1) (CUB).
local function CUB(Ps) return CSI .. Ps .. "D" end
-- CSI Ps ; Ps H Cursor Position [row;column] (default = [1,1]) (CUP).
local function CUP(row, col) return CSI .. row .. ";" .. col .. "H" end
-- CSI Ps ; Ps r Set Scrolling Region [top;bottom] (default = full size of window) (DECSTBM), VT100.
local function DECSTBM(top, bottom) return CSI .. top .. ";" .. bottom .. "r" end
-- CSI Ps L  Insert Ps Line(s) (default = 1) (IL).
local function IL(x) return CSI .. x .. "L" end
-- CSI Ps M  Delete Ps Line(s) (default = 1) (DL).
local function DL(x) return CSI .. x .. "M" end
-- CSI Ps P  Delete Ps Character(s) (default = 1) (DCH).
local function DCH(x) return CSI .. x .. "P" end
-- CSI Ps @  Insert Ps (Blank) Character(s) (default = 1) (ICH).
local function ICH(x) return CSI .. x .. "@" end
-- CSI Ps J  Erase in Display (ED), VT100.
local function ED(x) return CSI .. x .. "J" end
-- CSI Ps K  Erase in Line (EL), VT100.
local function EL(x) return CSI .. x .. "K" end
-- CSI Pm m  Character Attributes (SGR).
local function SGR(x) return CSI .. x .. "m" end
-- CSI Pm h  Set Mode (SM).
local function SM(x) return CSI .. x .. "h" end
-- CSI ? Pm h DEC Private Mode Set (DECSET).
local function DECSET(x) return CSI .. "?" .. x .. "h" end
-- CSI ? Pm l DEC Private Mode Reset (DECRST).
local function DECRST(x) return CSI .. "?" .. x .. "l" end
-- CSI Ps I  Cursor Forward Tabulation Ps tab stops (default = 1) (CHT).
local function CHT(n) return CSI .. n .. "I" end
-- CSI Ps Z  Cursor Backward Tabulation Ps tab stops (default = 1) (CBT).
local function CBT(n) return CSI .. n .. "Z" end
-- CSI Ps g  Tab Clear (TBC).
local function TBC(n) return CSI .. n .. "g" end
-- CSI Ps T  Scroll down Ps lines (default = 1) (SD), VT420.
local function SD(x) return CSI .. x .. "T" end
-- CSI Ps S  Scroll up Ps lines (default = 1) (SU), VT420, ECMA-48.
local function SU(x) return CSI .. x .. "S" end
-- CSI Ps X  Erase Ps Character(s) (default = 1) (ECH).
local function ECH(x) return CSI .. x .. "X" end

-- writing on the right-most column overwrites that column instead of wrapping.
local disable_wrapping = DECRST(7)
-- sending a newline moves the cursor to the leftmost column
local auto_return      = SM(20)

-- Reverse index -- move cursor down. Scroll if the cursor is at the bottom, creating a clear line.
local RI               = "\x1bM"
-- Index -- move cursor up. Scroll if the cursor hits is at the top, creating a clear line.
local IND              = "\x1bD"
-- fill screen with 'E' (DEC alignment test)
local DECALN           = "\x1b#8"
-- set tab stop at current column
local HTS              = "\x1bH"

-- Create and size a lua window; returns win_id.
local function make_window(width, height)
  local win_id = vv.api.window_create()
  vv.api.window_set_geometry(win_id, { left = 1, top = 1, width = width, height = height })
  return win_id
end

-- Assert the visible screen of win_id matches expected_rows (a table of row strings).
-- Rows shorter than the window width are right-padded with spaces; missing rows default
-- to all-spaces. Width and height are read from the window geometry.
local function assert_screen(testname, win_id, expected_rows)
  local geom = vv.api.window_get_geometry(win_id)
  local w, h = geom.width, geom.height
  local lines = vv.api.window_get_text(win_id, { left = 1, top = 1, width = w, height = h })
  for i = 1, h do
    local actual   = (lines[i] and lines[i].text or "")
    local expected = (expected_rows[i] or "")
    -- pad both to w so comparison is width-aware
    if #actual < w then actual = actual .. string.rep(" ", w - #actual) end
    if #expected < w then expected = expected .. string.rep(" ", w - #expected) end
    if actual ~= expected then
      error(string.format(
        "[%s] Row %d mismatch:\n  expected: %q\n  actual:   %q",
        testname, i, expected, actual
      ))
    end
  end
end

-- Return the cell contents of the first row as a plain array indexed by column (1-based).
-- Continuation cells of wide characters have a nil value at their position.
local function row_cells(win_id)
  local geom = vv.api.window_get_geometry(win_id)
  local line = vv.api.window_get_cells(win_id, { left = 1, top = 1, width = geom.width, height = geom.height })[1]
  local src = (line or {}).cells or {}
  -- Use rawset so nil entries are preserved as explicit holes.
  local result = {}
  for i, cell in ipairs(src) do
    rawset(result, i, cell.content) -- nil for wide-char continuation cells
  end
  return result
end

-- Helper: create a window, write input, assert screen, then close.
local function check(testname, width, height, input, expected_rows)
  local win_id = make_window(width, height)
  vv.api.window_write(win_id, input)
  assert_screen(testname, win_id, expected_rows)
  vv.api.window_close(win_id)
end

-- Convenience for the common 5-row × 8-col size used throughout these tests.
local function check8(testname, input, expected_rows)
  check(testname, 8, 5, input, expected_rows)
end

local function test_input_output() -- {{{1
  check8("single character", "x", { "x" })

  check8("wrapping", "abcdefghijk", {
    "abcdefgh",
    "ijk",
  })

  check8("cursor movement",
    CUU(123) .. CUB(123) .. CUF(1) .. CUD(1) .. "12" .. CUF(99) .. CUD(99) .. CUU(1) .. CUB(1) .. "3",
    {
      "        ",
      " 12     ",
      "        ",
      "      3 ",
      "        ",
    })

  -- Fill 4 lines worth of spaces to push virtual scroll then move cursor
  check8("cursor movement virtual scroll",
    string.rep(" ", 17 * 4) ..
    CUU(123) .. CUB(123) .. CUF(1) .. CUD(1) .. "12" .. CUF(99) .. CUD(99) .. CUU(1) .. CUB(1) .. "3",
    {
      "        ",
      " 12     ",
      "        ",
      "      3 ",
      "        ",
    })

  -- line 1 scrolls out of view
  check8("scrolling 1", "line1   line2   line3   line4   line5   l", {
    "line2   ",
    "line3   ",
    "line4   ",
    "line5   ",
    "l       ",
  })

  -- line 1 and 2 scroll out of view
  check8("scrolling 2", "line1   line2   line3   line4   line5   line6   ", {
    "line2   ",
    "line3   ",
    "line4   ",
    "line5   ",
    "line6   ",
  })

  check8("E test command", DECALN, {
    "EEEEEEEE",
    "EEEEEEEE",
    "EEEEEEEE",
    "EEEEEEEE",
    "EEEEEEEE",
  })

  check8("Clear Command", DECALN .. ED(2), {})

  check8("Clear Command 2", string.rep("w", 100) .. ED(2), {})

  check8("Off by one",
    "AAAAAAAABBBBBBBBCCCCCCCCDDDDDDDDEEEEEEEE" .. SGR(0),
    {
      "AAAAAAAA",
      "BBBBBBBB",
      "CCCCCCCC",
      "DDDDDDDD",
      "EEEEEEEE",
    })

  check8("Off by one 2",
    CUF(99) .. CUD(99) .. "Y" .. SGR(0) .. CUU(99) .. CUB(99) .. "X" .. SGR(0),
    {
      "X       ",
      "        ",
      "        ",
      "        ",
      "       Y",
    })

  check8("Normal Rendering",
    "Hello!\r\n" .. EL(0) .. "Second line\r\n" .. EL(0) .. "Third line\r\n" .. EL(0),
    {
      "Second l",
      "ine     ",
      "Third li",
      "ne      ",
    })

  check8("Carriage Return", "Hello!!\rworld", {
    "world!! ",
  })

  check8("Insert Lines",
    auto_return .. "Line1\nLine2\nLine3\nLine4\nLine5" .. CUP(2, 1) .. IL(2),
    {
      "Line1   ",
      "        ",
      "        ",
      "Line2   ",
      "Line3   ",
    })

  check8("Insert Lines Virtual",
    auto_return .. "Line1\nLine2\nLine3\nLine4\nLine5\nLine6\nLine7" .. CUP(2, 1) .. IL(2),
    {
      "Line3   ",
      "        ",
      "        ",
      "Line4   ",
      "Line5   ",
    })

  check8("Delete Lines",
    auto_return .. "Line1\nLine2\nLine3\nLine4\nLine5\nLine6" .. CUP(2, 1) .. DL(2),
    {
      "Line2   ",
      "Line5   ",
      "Line6   ",
    })

  check8("Delete Many Lines",
    auto_return .. "Line1\nLine2\nLine3\nLine4\nLine5\nLine6" .. CUP(2, 1) .. DL(10),
    {
      "Line2   ",
    })

  check8("Delete Many Lines 2",
    auto_return .. "Line1\nLine2\nLine3\nLine4\nLine5\nLine6" .. CUP(9, 1) .. DL(10),
    {
      "Line2   ",
      "Line3   ",
      "Line4   ",
      "Line5   ",
    })

  check8("Delete Lines All But Last",
    auto_return .. "Line1\nLine2\nLine3\nLine4\nLine5\nLine6" .. CUP(1, 2) .. DL(4),
    {
      "Line6   ",
    })

  check8("Insert Lines Then Delete",
    auto_return .. "Line1\nLine2\nLine3\nLine4\nLine5\nLine6" .. CUP(2, 1) .. DL(2) .. IL(1),
    {
      "Line2   ",
      "        ",
      "Line5   ",
      "Line6   ",
    })

  check8("Delete Lines Then Insert",
    auto_return .. "Line1\nLine2\nLine3\nLine4\nLine5\nLine6" .. CUP(2, 1) .. IL(2) .. DL(1),
    {
      "Line2   ",
      "        ",
      "Line3   ",
      "Line4   ",
    })

  check8("Overflow screen",
    "AAAAAAAA" .. "BBBBBBBB" .. "CCCCCCCC" .. "DDDDDDDD" .. "EEEEEEEEE",
    {
      "BBBBBBBB",
      "CCCCCCCC",
      "DDDDDDDD",
      "EEEEEEEE",
      "E       ",
    })

  check8("Fill screen",
    "AAAAAAAA" .. "BBBBBBBB" .. "CCCCCCCC" .. "DDDDDDDD" .. "EEEEEEEE",
    {
      "AAAAAAAA",
      "BBBBBBBB",
      "CCCCCCCC",
      "DDDDDDDD",
      "EEEEEEEE",
    })
end

local function test_erase() -- {{{1
  check8("Line Delete",
    "xxx" .. CUP(1, 2) .. EL(1) .. "\r\n" ..
    "xxxx" .. EL(2) .. "\r\n" ..
    "ababab" .. EL(0) .. "\r\n" ..
    "ababab" .. CUB(5) .. EL(0),
    {
      "  x     ",
      "        ",
      "ababab  ",
      "a       ",
    })

  check8("ED(1): Clear Start To Cursor (simple)", "www" .. ED(1), {})
  check8("ED(2): Clear Screen (simple)", "xxx" .. ED(2), {})
  check8("ED(0): Clear Cursor To End (simple)", "www" .. CUP(1, 1) .. ED(0), {})

  check8("ED(1): Clear Start To Cursor",
    string.rep("w", 41) .. ED(1), {})

  check8("ED(1): Clear Start To Cursor 2",
    string.rep("w", 40) .. CUP(5, 7) .. ED(1),
    {
      "        ",
      "        ",
      "        ",
      "        ",
      "       w",
    })

  check8("ED(0): Clear Cursor To End",
    string.rep("w", 41) .. CUP(1, 1) .. ED(0), {})

  check8("ED(0): Clear Cursor To End 2",
    string.rep("w", 41) .. CUP(1, 2) .. ED(0),
    { "w       " })

  check8("ED(2): Clear Screen",
    string.rep("w", 41) .. CUP(1, 1) .. ED(2), {})

  check8("Backspace 0", "w\b\bxy\b\bab", { "ab      " })
  check8("Backspace 1", "wwwww\b\b\bxxx\b\b\b\b\b\by", { "ywxxx   " })

  check8("Insert Blanks 1",
    "helloooooo" .. CUB(10) .. DCH(1),
    {
      "helloooo",
      "o       ",
    })

  check8("Insert Blanks 2",
    "www" .. CUB(3) .. DCH(1) .. ICH(7),
    { "       w" })

  check8("Line Truncated",
    "abcd\r" .. ICH(2),
    { "  abcd  " })

  check8("ECH",
    "wwwww" .. CUP(1, 2) .. ECH(2),
    { "w  ww   " })
end

local function test_scrolling() -- {{{1
  check8("Line Truncated (scroll)",
    "abcd\r" .. ICH(2),
    { "  abcd  " })

  check8("Reverse Index (RI)",
    DECALN .. CUP(3, 2) .. RI .. "xyz" .. RI .. RI,
    {
      "        ",
      "EEEEEEEE",
      "Exyz" .. "EEEE",
      "EEEEEEEE",
      "EEEEEEEE",
    })

  check8("Index (IND)",
    DECALN .. CUP(3, 2) .. IND .. "xyz" .. IND .. IND .. IND .. RI .. "a",
    {
      "EEEEEEEE",
      "Exyz" .. "EEEE",
      "EEEEEEEE",
      "    a   ",
      "        ",
    })

  check8("Scroll Down 1 (SD)", DECALN .. SD(1), {
    "        ",
    "EEEEEEEE",
    "EEEEEEEE",
    "EEEEEEEE",
    "EEEEEEEE",
  })

  check8("Scroll Down 2 (SD)", DECALN .. SD(3), {
    "        ",
    "        ",
    "        ",
    "EEEEEEEE",
    "EEEEEEEE",
  })

  check8("Scroll Down + Scroll Region (SD)", DECSTBM(2, 4) .. DECALN .. SD(2), {
    "EEEEEEEE",
    "        ",
    "        ",
    "EEEEEEEE",
    "EEEEEEEE",
  })

  check8("Scroll Up 1 (SU)", DECALN .. SU(1), {
    "EEEEEEEE",
    "EEEEEEEE",
    "EEEEEEEE",
    "EEEEEEEE",
    "        ",
  })

  check8("Scroll Up 2 (SU)", DECALN .. SU(3), {
    "EEEEEEEE",
    "EEEEEEEE",
    "        ",
    "        ",
    "        ",
  })

  check8("Scroll Up + Scroll Region (SU)", DECSTBM(2, 4) .. DECALN .. SU(2), {
    "EEEEEEEE",
    "EEEEEEEE",
    "        ",
    "        ",
    "EEEEEEEE",
  })
end

local function test_nowrap() -- {{{1
  -- ASCII: cursor stops at right margin, subsequent chars overwrite the last column.

  check8("nowrap ascii overflow", disable_wrapping .. "abcdefghijk", { "abcdefgk" })
  check8("nowrap ascii exact fit", disable_wrapping .. "abcdefgh", { "abcdefgh" })
  check8("nowrap ascii short", disable_wrapping .. "abc", { "abc     " })
  check8("nowrap ascii overwrite edge", disable_wrapping .. "abcdefghxyz", { "abcdefgz" })


  -- é overflow: all three é overwrite col 8 → last é wins
  do
    local win_id = make_window(8, 5)
    vv.api.window_write(win_id, disable_wrapping .. "abcdefg\xc3\xa9\xc3\xa9\xc3\xa9")
    local cells = row_cells(win_id)
    vv.api.window_close(win_id)
    expect_eq("\xc3\xa9", cells[8]) -- é at col 8
  end

  check8("nowrap unicode narrow exact",
    disable_wrapping .. "abcdefg\xc3\xa9",
    { "abcdefg\xc3\xa9" })

  -- α,β,γ (each 1 column) then ASCII overflow; last col = k (overwrite)
  do
    local win_id = make_window(8, 5)
    vv.api.window_write(win_id, disable_wrapping .. "αβγdefghijk")
    local cells = row_cells(win_id)
    vv.api.window_close(win_id)
    expect_eq("α", cells[1])
    expect_eq("β", cells[2])
    expect_eq("γ", cells[3])
    expect_eq("d", cells[4])
    expect_eq("g", cells[7])
    expect_eq("k", cells[8])
  end

  -- wide CJK: 中 and 文 are each 2 columns
  -- abc中文 → a=col1, b=col2, c=col3, 中=cols4-5, 文=cols6-7
  do
    local win_id = make_window(8, 5)
    vv.api.window_write(win_id, disable_wrapping .. "abc中文")
    local cells = row_cells(win_id)
    vv.api.window_close(win_id)
    expect_eq("a", cells[1])
    expect_eq("b", cells[2])
    expect_eq("c", cells[3])
    expect_eq("中", cells[4]) -- 中 (wide)
    expect_eq(nil, cells[5]) -- continuation of 中
    expect_eq("文", cells[6]) -- 文 (wide)
    expect_eq(nil, cells[7]) -- continuation of 文
  end

  -- wide character cannot start on last column: 中 at col 8 of 8 → silently ignored
  check8("nowrap wide at edge",
    disable_wrapping .. "abcdefg中",
    { "abcdefg " })

  -- re-enable wrap after disabling: row 1 uses nowrap, row 2+ uses wrap
  check8("nowrap then wrap",
    disable_wrapping .. "abcdefghijk" .. DECSET(7) .. "\r\n" .. "abcdefghijk",
    {
      "abcdefgk",
      "abcdefgh",
      "ijk     ",
    })

  -- newline still works in nowrap mode (auto_carriage = LNM on)
  check8("nowrap with newline",
    disable_wrapping .. auto_return .. "abcdefghijk\nxyz",
    {
      "abcdefgk",
      "xyz     ",
    })
end

local function test_reflow() -- {{{1
  do
    -- grow: write to 5-wide, then expand to 8-wide, then shrink back to 5-wide
    local win_id = make_window(5, 5)
    vv.api.window_write(win_id, "AAAAABBBBBCCCCCDDDDDEEEEE")

    assert_screen("reflow grow – small", win_id, {
      "AAAAA",
      "BBBBB",
      "CCCCC",
      "DDDDD",
      "EEEEE",
    })

    vv.api.window_set_geometry(win_id, { left = 1, top = 1, width = 8, height = 5 })
    assert_screen("reflow grow – large", win_id, {
      "AAAAABBB",
      "BBCCCCCD",
      "DDDDEEEE",
      "E       ",
    })

    vv.api.window_set_geometry(win_id, { left = 1, top = 1, width = 5, height = 5 })
    assert_screen("reflow grow – shrink back", win_id, {
      "AAAAA",
      "BBBBB",
      "CCCCC",
      "DDDDD",
      "EEEEE",
    })

    vv.api.window_close(win_id)
  end

  do
    -- shrink: write to 8-wide, then shrink to 5-wide
    local win_id = make_window(8, 5)
    vv.api.window_write(win_id,
      "AAAAAAAA" .. "BBBBBBBB" .. "CCCCCCCC" .. "DDDDDDDD" .. "EEEEEEEE")

    assert_screen("reflow shrink – large", win_id, {
      "AAAAAAAA",
      "BBBBBBBB",
      "CCCCCCCC",
      "DDDDDDDD",
      "EEEEEEEE",
    })

    vv.api.window_set_geometry(win_id, { left = 1, top = 1, width = 5, height = 5 })
    assert_screen("reflow shrink – small", win_id, {
      "BCCCC",
      "CCCCD",
      "DDDDD",
      "DDEEE",
      "EEEEE",
    })

    vv.api.window_close(win_id)
  end

  do
    -- shrink with explicit line endings (CRLF lines are not wrapped)
    local win_id = make_window(8, 5)
    vv.api.window_write(win_id, "AAAAAAA\r\nBB\r\nDDDDDDD")

    assert_screen("reflow shrink2 – large", win_id, {
      "AAAAAAA ",
      "BB      ",
      "DDDDDDD ",
    })

    vv.api.window_set_geometry(win_id, { left = 1, top = 1, width = 5, height = 5 })
    assert_screen("reflow shrink2 – small", win_id, {
      "AAAAA",
      "AA   ",
      "BB   ",
      "DDDDD",
      "DD   ",
    })

    vv.api.window_close(win_id)
  end
end

local function test_tabs() -- {{{1
  -- Helper: write input to a wide window and return cursor column (1-indexed).
  local function col_after(width, input)
    local win_id = make_window(width, 5)
    vv.api.window_write(win_id, input)
    local pos = vv.api.window_get_cursor_position(win_id)
    vv.api.window_close(win_id)
    return pos.col
  end

  -- Default tab stops are every 8 columns (cols 9, 17, 25, …).
  -- \t from col 1 jumps to col 9.
  expect_eq(9, col_after(80, "\t"))
  expect_eq(17, col_after(80, "\t\t"))
  expect_eq(25, col_after(80, "\t\t\t"))

  -- \t from mid-column snaps forward to the next stop.
  expect_eq(9, col_after(80, "abc\t"))        -- col 4 → next stop at 9
  expect_eq(17, col_after(80, "abcdefgh\t"))  -- col 9 is a stop (writing 8 chars lands there); next stop is 17
  expect_eq(17, col_after(80, "abcdefghi\t")) -- col 10 → next stop at 17

  -- \t at or past the last stop clamps to the right margin, does not wrap.
  expect_eq(80, col_after(80, string.rep("\t", 20)))

  expect_eq(17, col_after(80, CHT(2)))          -- 2 stops from col 1: 9, 17
  expect_eq(25, col_after(80, "abc" .. CHT(3))) -- 3 stops from col 4: 9, 17, 25

  -- CBT (CSI Ps Z): backward Ps tab stops.
  expect_eq(9, col_after(80, "\t\t" .. CBT(1))) -- forward to 17, back 1 → 9
  expect_eq(1, col_after(80, "\t\t" .. CBT(2))) -- forward to 17, back 2 → 1 (clamps at left)

  -- TBC 0: clear tab stop at cursor; TBC 3: clear all tab stops.
  -- Clear stop at col 9, then tab from col 1 should jump to col 17.
  expect_eq(17, col_after(80, "\t" .. TBC(0) .. "\r" .. "\t"))

  -- TBC 3: clear all stops; tab should clamp to right margin.
  expect_eq(80, col_after(80, TBC(3) .. "\t"))

  -- HTS (ESC H): set a tab stop at the current column.
  -- Move to col 5, set stop, tab back to col 1, then tab forward → lands on col 5.
  expect_eq(5, col_after(80, TBC(3) .. "    " .. HTS .. "\r" .. "\t"))

  -- DECST8C (CSI ? 5 W): reset tab stops to every-8 default.
  -- Clear all stops first, confirm they are gone, then reset and confirm they are back.
  local DECST8C = CSI .. "?5W"
  expect_eq(80, col_after(80, TBC(3) .. "\t"))           -- all stops cleared, \t clamps to right margin
  expect_eq(9, col_after(80, TBC(3) .. DECST8C .. "\t")) -- after reset, \t lands on col 9 again

  -- Tabs produce visible content at the correct columns.
  -- On a 16-wide window: \t→col9, x→col10, \t→col17 clamps to col16, y→col16.
  check("tab content", 16, 3,
    "\t" .. "x" .. "\t\t\t" .. "y",
    { "        x      y" })
end

-- TODO missing test coverage {{{1
--
-- The following implemented features have no Lua tests yet:
--
-- SGR attributes (window_get_cells → cell.style bitmask):
--   bold(1), faint(2), italic(3), underline(4), blink(5/6), reverse(7),
--   conceal(8), strikethrough(9), double-underline(21), reset(0),
--   individual resets (22–29), foreground/background colors (30–49, 90–107),
--   RGB colors (38/48 with sub-params), 256-color table (38/48;5;n)
--
-- Cursor save / restore:
--   DECSC/DECRC (ESC 7 / ESC 8) — saves position, SGR, wrap flag, origin flag
--   SCOSC/SCORC (CSI s / CSI u) — ANSI.SYS variant
--   Verify that saved SGR state is restored alongside cursor position
--
-- Alternate screen buffer:
--   DECSET/DECRST 1047 — switch without cursor save/restore
--   DECSET/DECRST 1049 — switch with cursor save/restore
--   Primary screen content must survive while alternate is active
--
-- Origin mode (DECOM, DECSET/DECRST 6):
--   CUP row/col are relative to the scroll region when origin mode is on
--   Cursor cannot move outside the scroll region in origin mode
--   DECSTBM resets cursor to top of scroll region
--
-- Cursor-absolute movement (untested despite being implemented):
--   CHA (CSI Ps G) — cursor character absolute (column only)
--   VPA (CSI Ps d) — line position absolute (row only)
--   CNL (CSI Ps E) — cursor next line: moves down and to column 1
--   CPL (CSI Ps F) — cursor preceding line: moves up and to column 1
--   HVP (CSI Pr ; Pc f) — same as CUP but from ANSI X3.64
--
-- REP (CSI Ps b):
--   Repeat the last printed character Ps times; wraps if auto-wrap is on
--
-- ED(3) — erase scrollback buffer (xterm extension, implemented)

local function test() -- {{{1
  test_input_output()
  test_erase()
  test_scrolling()
  test_nowrap()
  test_reflow()
  test_tabs()
end

return { test = test }

-- Modeline {{{1
-- vim: fdm=marker shiftwidth=2 foldlevel=0
