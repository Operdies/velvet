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

local function to_json_aux(value, guard, fmt)
  local tbl = {}
  if value == nil then return nil end
  fmt.level = fmt.level + 1

  local indent = string.rep(fmt.indent, fmt.level)

  if type(value) == 'string' then
    local esc = value:gsub('\\', '\\\\'):gsub('"', '\\"'):gsub('\n', '\\n'):gsub('\r', '\\r'):gsub('\t', '\\t')
    tbl[1] = '"' .. esc .. '"'
  elseif type(value) == 'table' then
    if guard[value] then goto ret end
    if type(value) == 'table' then guard[value] = true end
    if is_array(value) then
      for _, val in ipairs(value) do
        local repr = to_json_aux(val, guard, fmt)
        if repr ~= nil then table.insert(tbl, repr) else table.insert(tbl, "null") end
      end
      if #tbl == 1 and not tbl[1]:match('\n') then
        tbl[1] = '[' .. tbl[1] .. ']'
      else
        tbl[1] = '[' .. fmt.newline .. indent .. tbl[1]
        tbl[#tbl] = tbl[#tbl] .. fmt.newline .. string.rep(fmt.indent, fmt.level - 1) .. ']'
      end
    else
      for k, v in pairs(value) do
        if k and is_primitive(k) then
          local repr = to_json_aux(v, guard, fmt)
          if repr ~= nil then
            local key_repr = type(k) == 'string' and to_json_aux(k, guard, fmt) or ('"' .. tostring(k) .. '"')
            table.insert(tbl, ('%s:%s%s'):format(key_repr, fmt.compact and "" or " ", repr))
          end
        end
      end
      if #tbl == 1 and not tbl[1]:match('\n') then
        tbl[1] = '{' .. tbl[1] .. '}'
      elseif #tbl >= 1 then
        tbl[1] = '{' .. fmt.newline .. indent .. tbl[1]
        tbl[#tbl] = tbl[#tbl] .. fmt.newline .. string.rep(fmt.indent, fmt.level - 1) .. '}'
      else
        tbl[1] = '{}'
      end
    end
    if type(value) == 'table' then guard[value] = nil end
  elseif type(value) == 'function' then
    -- explicitly ignore functions
  elseif type(value) == 'number' then
    local number_repr = tostring(value)
    if value ~= value or value == math.huge or value == -math.huge then
      number_repr = '"' .. number_repr .. '"'
    end
    tbl[1] = number_repr
  else -- integer, number, boolean
    -- tostring() correctly handles integer/float representations; integers have no decimal parts, integer floats do.
    tbl[1] = tostring(value)
  end
  ::ret::
  fmt.level = fmt.level - 1
  return #tbl == 0 and nil or table.concat(tbl, ',' .. (fmt.compact and '' or ' ') .. fmt.newline .. indent)
end

--- convert |tbl| to a json string
--- @param value any
--- @param compact? boolean
function M.to_json(value, compact)
  local fmt = compact and { indent = '', newline = '', level = 0 } or { indent = '  ', newline = '\n', level = 0 }
  fmt.compact = compact
  local guard = {}
  return to_json_aux(value, guard, fmt)
end

return M
