local inspect = require('velvet.inspect')
local t = {X = 123}
local t1 = { __tostring = function(tbl) return tbl.X end }
setmetatable(t, t1)
print(inspect(t))

