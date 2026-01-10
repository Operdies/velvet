local t = {X = 123}
local t1 = { 
  __index = function(tbl, key) 
    print('read index: ' .. key)
    if key == 'Z' then return function() print("Invoked returned function") end end
  end,
  __newindex = function(tbl, key, value)
    print('assign index: ' .. key .. ' = ' .. value)
  end
}
setmetatable(t, t1)

t.W = 1
local y = t.Y
t.Z()
