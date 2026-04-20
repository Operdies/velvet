local store = vv.api.runtime_store_value
local load = vv.api.runtime_load_value

-- primitive types
store('int', 99)
assert(load('int') == 99)

store('float', 1.5)
assert(load('float') == 1.5)

store('bool_true', true)
store('bool_false', false)
assert(load('bool_true') == true)
assert(load('bool_false') == false)

store('str', 'hello')
assert(load('str') == 'hello')

-- string edge cases
store('str_quotes', 'say "hello" it\'s fine')
assert(load('str_quotes') == 'say "hello" it\'s fine')

store('str_newline', 'line1\nline2')
assert(load('str_newline') == 'line1\nline2')

-- missing key returns nil
assert(load('nonexistent') == nil)

-- overwrite
store('overwrite', 1)
store('overwrite', 2)
assert(load('overwrite') == 2)

-- delete by setting nil
store('delete_me', 42)
store('delete_me', nil)
assert(load('delete_me') == nil)

-- simple table
store('tbl', { x = 1, y = 2 })
local t = load('tbl')
assert(t.x == 1)
assert(t.y == 2)

-- integer keys
store('arr', { 10, 20, 30 })
t = load('arr')
assert(t[1] == 10)
assert(t[2] == 20)
assert(t[3] == 30)

-- mixed keys
store('mixed', { 1, 2, x = 'hello', [10] = true })
t = load('mixed')
assert(t[1] == 1)
assert(t[2] == 2)
assert(t.x == 'hello')
assert(t[10] == true)

-- nested tables
store('nested', { a = { b = { c = 42 } } })
t = load('nested')
assert(t.a.b.c == 42)

-- integer vs float subtype preservation
store('numtypes', { i = 1, f = 1.0 })
t = load('numtypes')
assert(math.type(t.i) == 'integer')
assert(math.type(t.f) == 'float')

-- special float values
store('inf', math.huge)
store('neginf', -math.huge)
store('nan', 0 / 0)
assert(load('inf') == math.huge)
assert(load('neginf') == -math.huge)
assert(load('nan') ~= load('nan')) -- nan ~= nan

-- empty table
store('empty', {})
t = load('empty')
assert(type(t) == 'table')

-- big nested structure
local original = {
  i = 42,
  f = 3.14,
  s = 'hello world',
  b_true = true,
  b_false = false,
  inf = math.huge,
  neginf = -math.huge,
  nan = 0/0,
  [1] = 'one',
  [2] = 'two',
  [100] = 'sparse',
  quoted = 'say "hello" it\'s fine',
  layout = {
    x = 10,
    y = 20,
    size = 1.5,
    tags = { 'visible', 'active', 'focused' },
    children = {
      { name = 'panel_a', width = 100, height = 200, visible = true },
      { name = 'panel_b', width = 300, height = 400, visible = false },
    },
  },
  prefs = {
    volume = 0.75,
    theme = 'dark',
    keymap = {
      save = 'ctrl+s',
      quit = 'ctrl+q',
      reload = 'ctrl+r',
    },
  },
}
store('big', original)
t = load('big')
assert(t.i == 42, 'i: expected 42, got ' .. tostring(t.i))
assert(math.type(t.i) == 'integer', 'i: expected integer subtype, got ' .. math.type(t.i))
assert(t.f == 3.14, 'f: expected 3.14, got ' .. tostring(t.f))
assert(math.type(t.f) == 'float', 'f: expected float subtype, got ' .. math.type(t.f))
assert(t.s == 'hello world', 's: expected "hello world", got ' .. tostring(t.s))
assert(t.b_true == true, 'b_true: expected true, got ' .. tostring(t.b_true))
assert(t.b_false == false, 'b_false: expected false, got ' .. tostring(t.b_false))
assert(t.inf == math.huge, 'inf: expected math.huge, got ' .. tostring(t.inf))
assert(t.neginf == -math.huge, 'neginf: expected -math.huge, got ' .. tostring(t.neginf))
assert(t.nan ~= t.nan, 'nan: expected NaN (not equal to itself)')
assert(t[1] == 'one', '[1]: expected "one", got ' .. tostring(t[1]))
assert(t[2] == 'two', '[2]: expected "two", got ' .. tostring(t[2]))
assert(t[100] == 'sparse', '[100]: expected "sparse", got ' .. tostring(t[100]))
assert(t.quoted == 'say "hello" it\'s fine', 'quoted: expected correct string, got ' .. tostring(t.quoted))
assert(t.layout.x == 10, 'layout.x: expected 10, got ' .. tostring(t.layout.x))
assert(t.layout.y == 20, 'layout.y: expected 20, got ' .. tostring(t.layout.y))
assert(t.layout.size == 1.5, 'layout.size: expected 1.5, got ' .. tostring(t.layout.size))
assert(t.layout.tags[1] == 'visible', 'layout.tags[1]: expected "visible", got ' .. tostring(t.layout.tags[1]))
assert(t.layout.tags[2] == 'active', 'layout.tags[2]: expected "active", got ' .. tostring(t.layout.tags[2]))
assert(t.layout.tags[3] == 'focused', 'layout.tags[3]: expected "focused", got ' .. tostring(t.layout.tags[3]))
assert(t.layout.children[1].name == 'panel_a', 'children[1].name: expected "panel_a", got ' .. tostring(t.layout.children[1].name))
assert(t.layout.children[1].width == 100, 'children[1].width: expected 100, got ' .. tostring(t.layout.children[1].width))
assert(t.layout.children[1].height == 200, 'children[1].height: expected 200, got ' .. tostring(t.layout.children[1].height))
assert(t.layout.children[1].visible == true, 'children[1].visible: expected true, got ' .. tostring(t.layout.children[1].visible))
assert(t.layout.children[2].name == 'panel_b', 'children[2].name: expected "panel_b", got ' .. tostring(t.layout.children[2].name))
assert(t.layout.children[2].width == 300, 'children[2].width: expected 300, got ' .. tostring(t.layout.children[2].width))
assert(t.layout.children[2].height == 400, 'children[2].height: expected 400, got ' .. tostring(t.layout.children[2].height))
assert(t.layout.children[2].visible == false, 'children[2].visible: expected false, got ' .. tostring(t.layout.children[2].visible))
assert(t.prefs.volume == 0.75, 'prefs.volume: expected 0.75, got ' .. tostring(t.prefs.volume))
assert(t.prefs.theme == 'dark', 'prefs.theme: expected "dark", got ' .. tostring(t.prefs.theme))
assert(t.prefs.keymap.save == 'ctrl+s', 'keymap.save: expected "ctrl+s", got ' .. tostring(t.prefs.keymap.save))
assert(t.prefs.keymap.quit == 'ctrl+q', 'keymap.quit: expected "ctrl+q", got ' .. tostring(t.prefs.keymap.quit))
assert(t.prefs.keymap.reload == 'ctrl+r', 'keymap.reload: expected "ctrl+r", got ' .. tostring(t.prefs.keymap.reload))
