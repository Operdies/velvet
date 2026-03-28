local function merge_keep(key, prev_value, value)
  return prev_value
end

local function merge_force(key, prev_value, value)
  return value
end

local function merge_error(key, prev_value, value)
  error('key found in more than one map: ' .. key)
end

local behaviors = {
  error = merge_error,
  keep = merge_keep,
  force = merge_force,
}

local function merge_rec(obj, guard, resolve, ...)
  for i, tbl in ipairs({ ... }) do
    for k, v in pairs(tbl) do
      if type(v) == 'table' then
        if guard[v] then error("deep_extend does not support self-referencing tables.") end
        -- guard against this table on recursive calls
        guard[v] = true
        if type(obj[k]) == 'table' then
          -- if obj[k] is already a table, we need to merge with the new table
          obj[k] = merge_rec(obj[k], guard, resolve, v)
        else
          -- otherwise if obj[k] is a non-table value we need to resolve() the
          -- current and new value.
          local new = merge_rec({}, guard, resolve, v)
          obj[k] = obj[k] == nil and new or resolve(k, obj[k], new)
        end
        -- it is fine to reference this table again now that we are no longer recursing
        guard[v] = nil
      else
        if obj[k] ~= nil then
          obj[k] = resolve(k, obj[k], v)
        else
          obj[k] = v
        end
      end
    end
  end
  return obj
end


---@param behavior 'error'|'keep'|'force'|fun(key:any, prev_value:any?, value:any): any Decides what to do if a key is found in more than one map:
---      - "error": raise an error
---      - "keep":  use value from the leftmost map
---      - "force": use value from the rightmost map
---      - If a function, it receives the current key, the previous value in the currently merged table (if present), the current value and should
---        return the value for the given key in the merged table.
---@param ... table Two or more tables
---@return table Merged table
return function(behavior, ...)
  local resolve_func = behaviors[behavior] or behavior
  assert(type(resolve_func) == "function", "bad argument #1 ('error'|'keep'|'force'|function expected)")
  return merge_rec({}, {}, resolve_func, ...)
end
