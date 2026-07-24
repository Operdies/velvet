local M = {}

local function string_lines(str)
  if not str then return {} end
  if (type(str) == 'table') then return str end
  local lines = {}
  for s in str:gmatch("[^\r\n]+") do
    table.insert(lines, s)
  end
  return lines
end

function M.string_concatenate(tbl, after)
  if type(tbl) == 'string' then return tbl end
  local str = ""
  for i, s in ipairs(tbl or {}) do
    str = str .. s
    if i < #tbl then str = str .. after end
  end
  return str
end

function M.get_cname(name)
  return "velvet_api_" .. name:gsub("%.", "_")
end

function M.get_luaname(name)
  return "velvet.api." .. name
end

function M.enum_value_c_name(enum, option)
  return ("%s_%s"):format(enum, option):upper()
end

local function string_replace(template_string, template_args)
  for k, v in pairs(template_args or {}) do
    local pat = '<' .. k .. '>'
    template_string = template_string:gsub(pat, v)
  end
  local unresolved = template_string:match("<[%w_]+>")
  if unresolved then error("Template string has unresolved patterns: " .. unresolved) end
  return template_string
end

--- @param table string[]
--- @param template_string string
--- @param template_args? table<string, string|string[]|integer>
local function table_insert_template(table, template_string, template_args)
  local string = string_replace(template_string, template_args)
  table[#table + 1] = string
end

--- @class gen_type
--- @field c_type string c name of the type
--- @field lua_type string lua name of the type
--- @field check? fun(idx: integer): string c code for checking and retrieving the value at stack position |idx|
--- @field check_shift? fun(idx: string, idx2: string): string c code for checking and retrieving the value at stack position |idx|
--- @field push? fun(var: string): string c code for pushing a named variable to the stack
--- @field composite? spec_type
--- @field enumeration? spec_enum
--- @field optional? boolean

-- Type Utilities {{{1

local function check_function_and_shift(idx1, idx2)
  return string.format("luaL_checkfunction_and_shift(L, %s, %s)", idx1, idx2)
end

local function check_table_and_shift(idx1, idx2)
  return string.format("luaL_checktable_and_shift(L, %s, %s)", idx1, idx2)
end


--- @type table<string,gen_type>
local type_lookup = {
  any = { lua_type = "any", c_type = "void" },
  ["table"] = {
    c_type = "lua_stackIndex",
    lua_type = "table",
    check = function(idx) return ("luaL_checktable(L, %d)"):format(idx) end,
  },
  ["table<string, string>"] = {
    c_type = "lua_stackIndex",
    lua_type = "table<string, string>",
    check = function(idx) return ("luaL_checktable(L, %d)"):format(idx) end,
    check_shift = check_table_and_shift,
  },
  ["int[]"] = { lua_type = "integer[]", c_type = "int[]" },
  ["string|string[]"] = { lua_type = "string|string[]", c_type = "void*" },
  ["string[]"] = { lua_type = "string[]", c_type = "void*" },
  void = { c_type = "void", lua_type = "nil" },
  ["function"] = {
    c_type = "lua_Integer",
    lua_type = "fun(): nil",
    check = function(idx) return ("luaL_checkfunction(L, %d)"):format(idx) end,
  },
  int = {
    c_type = "lua_Integer",
    lua_type = "integer",
    check = function(idx) return ("luaL_checkinteger(L, %d)"):format(idx) end,
    push = function(var) return ("lua_pushinteger(L, %s)"):format(var) end
  },
  string = {
    c_type = "struct u8_slice",
    lua_type = "string",
    check = function(idx) return ("luaL_checkslice(L, %d)"):format(idx) end,
    push = function(var) return ("lua_pushslice(L, %s)"):format(var) end
  },
  bool = {
    c_type = "bool",
    lua_type = "boolean",
    check = function(idx) return ("luaL_checkboolean(L, %d)"):format(idx) end,
    push = function(var) return ("lua_pushboolean(L, %s)"):format(var) end
  },
  float = {
    c_type = "float",
    lua_type = "number",
    check = function(idx) return ("luaL_checknumber(L, %d)"):format(idx) end,
    push = function(var) return ("lua_pushnumber(L, %s)"):format(var) end
  },
}

M.type_lookup = type_lookup

function M.lua_type(t)
  if t == nil then return "nil" end
  return type_lookup[t].lua_type
end

function M.lua_check(t, idx, idx2)
  local tp = type_lookup[t]
  if idx2 and tp.check_shift then return tp.check_shift(idx, idx2) end
  return tp.check(idx)
end

function M.lua_push(t, var)
  if not type_lookup[t].push then error(("lua type '%s' not defined."):format(var)) end
  return type_lookup[t].push(var)
end

local spec = require('spec')
local spec_initialized = false

--- @return spec
function M.spec()
  if spec_initialized then return spec end
  spec_initialized = true
  for _, fn in ipairs(spec.options) do
    fn.doc = string_lines(fn.doc)
    local getter = {
      name = "get_" .. fn.name,
      doc = ("Get %s"):format(fn.name),
      params = {},
      returns = { type = fn.type, doc = "current " .. fn.name:gsub('_', ' '), name = fn.name }
    }
    local setter = {
      name = "set_" .. fn.name,
      doc = ("Set %s. Returns the new value."):format(fn.name),
      params = { { name = "new_value", type = fn.type, doc = fn.doc } },
      returns = { type = fn.type, doc = "new " .. fn.name:gsub('_', ' '), name = fn.name }
    }
    table.insert(spec.api, getter)
    table.insert(spec.api, setter)
  end

  for _, fn in ipairs(spec.api) do
    fn.params = fn.params or {}
    fn.returns = fn.returns or { type = "void", name = '' }

    for _, p in ipairs(fn.params) do
      p.doc = string_lines(p.doc)
    end
    fn.doc = string_lines(fn.doc)
    fn.returns.doc = string_lines(fn.returns.doc)
  end

  --- @param type spec_type
  --- @return boolean
  local function compute_is_optional(type)
    for _, fld in pairs(type.fields) do
      if fld.optional ~= true then return false end
    end
    return true
  end

  for _, type in ipairs(spec.types) do
    local entry = { c_type = "struct " .. M.get_cname(type.name), lua_type = M.get_luaname(type.name), composite = type }
    entry.optional = compute_is_optional(type)
    type_lookup[type.name] = entry

    -- limited support for arrays of objects.
    -- These types cannot be automatically marshalled, but we still need to generate docs.
    local array_entry = { lua_type = entry.lua_type .. '[]' }
    type_lookup[type.name .. '[]'] = array_entry
  end

  for _, cb in ipairs(spec.callbacks) do
    local args = {}
    for _, arg in ipairs(cb.params) do
      local template = { name = arg.name, type = arg.type .. (arg.optional and '|nil' or '') }
      args[#args + 1] = string_replace("<name>: <type>", template)
    end
    local template = { args = table.concat(args, ", "), ret = cb.returns and cb.returns.type or 'nil' }
    local signature = string_replace("fun(<args>): <ret>", template)
    local entry = { c_type = "lua_Integer", lua_type = signature, check_shift = check_function_and_shift }
    type_lookup[cb.name] = entry
  end

  for _, type in ipairs(spec.enums) do
    type_lookup[type.name] = {
      c_type = "enum " .. M.get_cname(type.name),
      lua_type = M.get_luaname(type.name),
      enumeration =
          type
    }
  end

  -- hack to avoid errors when assigning `palette.x = '#rrggbb'
  -- TODO: Extend spec to allow optional types and either handle that in C or in lua adapters
  type_lookup['rgb_color'].lua_type = type_lookup['rgb_color'].lua_type .. '|string'

  return spec
end

--- @class builder
--- @field indent integer
--- @field [integer] string
local builder = {}
builder.__index = builder

function builder:push(template, args)
  local indent = string.rep(' ', self.indent)
  template = (indent .. template):gsub("(\n)([^\n])", "%1" .. indent .. "%2")
  table_insert_template(self, template, args)
end

function builder:tostring()
  for i, v in ipairs(self) do
    if v:match('^%s*$') then self[i] = '' end
  end
  return table.concat(self, '\n')
end

function builder.create()
  return setmetatable({ indent = 0 }, builder)
end

M.builder = builder.create
M.string_replace = string_replace

-- types we know that we cannot automatically marshal. Such functions must be implemented by hand.
local manual_types = {
  ["cell_line[]"] = true,
  ["cell_line"] = true,
  cell = true,
  ['cell[]'] = true,
  ["int[]"] = true,
  any = true,
  ["string[]"] = true,
  ["line[]"] = true,
  ["string|string[]"] = true,
  ["table"] = true,
  ["table<string, string>"] = true,
}

function M.is_manual(name)
  return manual_types[name]
end

function M.c_type(t)
  local entry = type_lookup[t] or error("Unrecognized type: " .. t)
  if not entry.c_type then error('Cannot handle type: ' .. t) end
  return entry.c_type
end

local push = {}

--- @param tbl builder
--- @param type string
--- @param path string
function push.composite(tbl, type, path)
  local tp = M.type_lookup[type]
  local template = { path = path, name = tp.composite.name }
  tbl:push('lua_newtable(L); /* <path> = new <name> */', template)
  for _, mem in ipairs(tp.composite.fields) do
    local mem_path = path .. "." .. mem.name
    if mem.optional then mem_path = mem_path .. '.value' end
    push.field(tbl, mem.type, mem_path)
    tbl:push('lua_setfield(L, -2, "<name>"); /* <path> = <name> */', { name = mem.name, path = mem_path })
  end
end

--- @param tbl builder
--- @param type string
--- @param path string
function push.enumeration_flags(tbl, type, path)
  local tp = M.type_lookup[type]
  local template = { path = path, flag = tp.enumeration.name }
  tbl:push('lua_newtable(L); /* <path> = <flag> flags */', template)
  local enum_name = M.get_cname(type)

  template = { value = path, type = type }
  for _, flag in ipairs(tp.enumeration.values) do
    template.flag = M.enum_value_c_name(enum_name, flag.name)
    tbl:push([[
if (<value> & <flag>) {
  lua_pushslice(L, <type>_to_slice(<flag>));
  lua_pushboolean(L, true);
  lua_settable(L, -3);
}
]], template)
  end
end

--- @param tbl builder
--- @param type string
--- @param path string
function push.enumeration(tbl, type, path)
  local tp = M.type_lookup[type]
  if tp.enumeration.flags then
    push.enumeration_flags(tbl, type, path)
    return
  end
  local template = { name = tp.enumeration.name, value = path }
  tbl:push('lua_pushslice(L, <name>_to_slice(<value>));', template)
end

-- recursively marshal a C struct into a lua table
-- The marshalling code is written as a string to tbl
--- @param tbl builder
--- @param type string
--- @param path string
function push.field(tbl, type, path)
  local tp = M.type_lookup[type]
  if tp.composite then
    push.composite(tbl, type, path)
  elseif tp.enumeration then
    push.enumeration(tbl, type, path)
  else
    tbl:push('<push>;', { push = M.lua_push(type, path) })
  end
end

local check = {}

--- @param type_name string
--- @param path string
function check.composite(type_name, path)
  local result = M.builder()
  local type = M.type_lookup[type_name]
  if not type.optional then
    result:push('luaL_checktype(L, -1, LUA_TTABLE);')
  end
  for _, mem in ipairs(type.composite.fields) do
    local mem_path = path .. "." .. mem.name
    result:push('lua_getfield(L, -1, "<name>"); /* get <path> */', { name = mem.name, path = mem_path })
    if mem.optional then
      result:push('\nif (!lua_isnoneornil(L, -1)) {')
      result.indent = result.indent + 2
      result:push('<path>.set = true;', { path = mem_path })
      check.field(result, mem.type, mem_path .. '.value')
      result.indent = result.indent - 2
      result:push('}\n')
    else
      check.field(result, mem.type, mem_path)
    end
    result:push('lua_pop(L, 1); /* pop <path> */', { path = mem_path })
  end
  return result:tostring()
end

--- @param type_name string
--- @param path string
function check.enumeration(type_name, path)
  local result = M.builder()
  local type = M.type_lookup[type_name]
  if type.enumeration.flags then
    local enum_name = M.get_cname(type.enumeration.name)
    result:push('luaL_checktype(L, -1, LUA_TTABLE);')
    for _, flag in ipairs(type.enumeration.values) do
      local c_name = M.enum_value_c_name(enum_name, flag.name)
      result:push([[
lua_getfield(L, -1, "<flag_name>");
if (!lua_isnoneornil(L, -1) && luaL_checkboolean(L, -1))
  <path> |= <c_name>;
lua_pop(L, 1);
]], { c_name = c_name, flag_name = flag.name, path = path })
    end
  else
    result:push([[
struct u8_slice <name> = luaL_checkslice(L, -1);
int <type>_conv = <type>_slice_to_enum(<name>);
if (<type>_conv == ~0) {
  lua_pushstring(L, " is not a valid <type> value.");
  lua_concat(L, 2);
  lua_error(L);
}
<path> = <type>_conv;]], { name = path:gsub('%.', '_') .. '_str', type = type_name, path = path })
  end

  return result:tostring()
end

--- @param type_name string
--- @param path string
function check.primitive(type_name, path)
  return M.string_replace('<path> = <check>;', { path = path, check = M.lua_check(type_name, -1, "++argtop") })
end

--- @param tbl builder
--- @param type_name string
--- @param path string
function check.field(tbl, type_name, path)
  local type = M.type_lookup[type_name]
  local checked = nil
  if type and type.composite then
    checked = check.composite(type_name, path)
  elseif type and type.enumeration then
    checked = check.enumeration(type_name, path)
  else
    checked = check.primitive(type_name, path)
  end
  tbl:push(checked)
end

M.push = push
M.check = check

return M
