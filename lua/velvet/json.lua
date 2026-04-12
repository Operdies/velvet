local M = {}

local function is_array(t)
  local n = #t
  for k in pairs(t) do
    if math.type(k) ~= 'integer' or k < 1 or k > n then
      return false
    end
  end
  return n > 0
end

local function is_primitive(v)
  local is_complex = type(v) == 'function' or type(v) == 'table'
  return not is_complex
end

local function to_json_aux(value, guard)
  local tbl = {}
  if value == nil then return nil end

  if type(value) == 'string' then
    local esc = value:gsub('\\', '\\\\'):gsub('"', '\\"'):gsub('\n', '\\n'):gsub('\r', '\\r'):gsub('\t', '\\t')
    return '"' .. esc .. '"'
  elseif type(value) == 'table' then
    if guard[value] then return nil end
    if type(value) == 'table' then guard[value] = true end
    if is_array(value) then
      for _, val in ipairs(value) do
        local repr = to_json_aux(val, guard)
        if repr ~= nil then table.insert(tbl, repr) else table.insert(tbl, "null") end
      end
      tbl[1] = '[' .. tbl[1]
      tbl[#tbl] = tbl[#tbl] .. ']'
    else
      for k, v in pairs(value) do
        if k and is_primitive(k) then
          local repr = to_json_aux(v, guard)
          if repr ~= nil then
            local key_repr = type(k) == 'string' and to_json_aux(k, guard) or ('"' .. tostring(k) .. '"')
            table.insert(tbl, ('%s:%s'):format(key_repr, repr))
          end
        end
      end
      if #tbl > 0 then
        tbl[1] = '{' .. tbl[1]
        tbl[#tbl] = tbl[#tbl] .. '}'
      else
        tbl[1] = '{}'
      end
    end
    if type(value) == 'table' then guard[value] = nil end
    return table.concat(tbl, ',')
  elseif type(value) == 'function' then
    -- explicitly ignore functions
  elseif type(value) == 'number' then
    local number_repr = tostring(value)
    if value ~= value or value == math.huge or value == -math.huge then
      number_repr = '"' .. number_repr .. '"'
    end
    return number_repr
  else -- integer, number, boolean
    -- tostring() correctly handles integer/float representations; integers have no decimal parts, integer floats do.
    return tostring(value)
  end
end

--- convert |tbl| to a json string
--- @param value any
function M.to_json(value)
  local guard = {}
  return to_json_aux(value, guard)
end

return M
