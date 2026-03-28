local deep_extend = require('velvet.lib.deep_extend')

-- basic merge, disjoint keys
local r = deep_extend('error', { a = 1 }, { b = 2 })
assert(r.a == 1 and r.b == 2)

-- nested tables merge
local r2 = deep_extend('error', { x = { one = 1 } }, { x = { two = 2 } })
assert(r2.x.one == 1 and r2.x.two == 2)

-- force picks rightmost
local r3 = deep_extend('force', { a = 1 }, { a = 2 })
assert(r3.a == 2)

-- keep picks leftmost
local r4 = deep_extend('keep', { a = 1 }, { a = 2 })
assert(r4.a == 1)

-- error on duplicate leaf
local ok, _ = pcall(deep_extend, 'error', { a = 1 }, { a = 2 })
assert(not ok)

-- false values handled correctly (not treated as nil)
local r5 = deep_extend('force', { a = false }, { a = true })
assert(r5.a == true)
local r6 = deep_extend('keep', { a = false }, { a = true })
assert(r6.a == false)

-- inputs are not mutated
local orig = { nested = { x = 1 } }
local _ = deep_extend('force', orig, { nested = { y = 2 } })
assert(orig.nested.y == nil, 'input was mutated')

-- self-referencing table detection
local cycle = {}
cycle.self = cycle
local ok2, _ = pcall(deep_extend, 'force', cycle)
assert(not ok2)

-- three-way merge
local r7 = deep_extend('force', { a = 1 }, { b = 2 }, { c = 3 })
assert(r7.a == 1 and r7.b == 2 and r7.c == 3)

-- scalar vs table conflict (force: table wins as rightmost)
local r8 = deep_extend('force', { a = 5 }, { a = { nested = 1 } })
assert(type(r8.a) == 'table' and r8.a.nested == 1)
