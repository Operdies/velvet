-- Setup {{{1
local spec_path = assert(arg[1], "usage: lua api_gen.lua <api_spec.lua> <out_dir>")
local out_dir = assert(arg[2], "usage: lua api_gen.lua <api_spec.lua> <out_dir>")

--- @type spec
local spec = dofile(spec_path)

local inspect = dofile("lua/velvet/inspect.lua")

out_dir = out_dir:gsub("/$", "", 1)

local function write_file(path, contents)
  local f = assert(io.open(path, "w"))
  f:write(contents)
  f:close()
end

local function ensure_dir(path)
  return os.execute(string.format('mkdir -p "%s"', path))
end

ensure_dir(out_dir)

local function string_lines(str)
  if not str then return {} end
  if (type(str) == 'table') then return str end
  local lines = {}
  for s in str:gmatch("[^\r\n]+") do
    table.insert(lines, s)
  end
  return lines
end

local function string_concatenate(tbl, after)
  if type(tbl) == 'string' then return tbl end
  local str = ""
  for i, s in ipairs(tbl or {}) do
    str = str .. s
    if i < #tbl then str = str .. after end
  end
  return str
end


for _, fn in ipairs(spec.options) do
  fn.doc = string_lines(fn.doc)
  local getter = {
    name = "get_" .. fn.name,
    doc = ("Get %s"):format(fn.name),
    params = {},
    returns = { type = fn.type, doc = "The current value" }
  }
  local setter = {
    name = "set_" .. fn.name,
    doc = ("Set %s. Returns the new value."):format(fn.name),
    params = { { name = "new_value", type = fn.type, doc = fn.doc } },
    returns = { type = fn.type, doc = "The value after the update" }
  }
  table.insert(spec.api, getter)
  table.insert(spec.api, setter)
end

for _, fn in ipairs(spec.api) do
  fn.params = fn.params or {}
  fn.returns = fn.returns or { type = "void" }

  for _, p in ipairs(fn.params) do
    p.doc = string_lines(p.doc)
  end
  fn.doc = string_lines(fn.doc)
  fn.returns.doc = string_lines(fn.returns.doc)
end

--- @class gen_type
--- @field c_type string c name of the type
--- @field lua_type string lua name of the type
--- @field check? fun(idx: integer): string c code for checking and retrieving the value at stack position |idx|
--- @field push? fun(var: string): string c code for pushing a named variable to the stack
--- @field composite? spec_type
--- @field enumeration? spec_enum
--- @field optional? boolean

-- Type Utilities {{{1

--- @type table<string,gen_type>
local type_lookup = {
  any = { lua_type = "any", c_type = "void" },
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

local function get_cname(name)
  return "velvet_api_" .. name:gsub("[.]", "_")
end

local function get_luaname(name)
  return "velvet.api." .. name
end

local function enum_value_c_name(enum, option)
  return ("%s_%s"):format(enum, option):upper()
end

local function is_manual(name) 
  -- types we know that we cannot automatically marshal. Such functions must be implemented by hand.
  local manual_types = { ["int[]"] = true, any = true, ["string[]"] = true, ["line[]"] = true, ["string|string[]"] = true }
  return manual_types[name]
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
  local entry = { c_type = "struct " .. get_cname(type.name), lua_type = get_luaname(type.name), composite = type }
  entry.optional = compute_is_optional(type)
  type_lookup[type.name] = entry

  -- limited support for arrays of objects. 
  -- These types cannot be automatically marshalled, but we still need to generate docs.
  local array_entry = { lua_type = entry.lua_type .. '[]' }
  type_lookup[type.name .. '[]'] = array_entry
end

for _, type in ipairs(spec.enums) do
  type_lookup[type.name] = { c_type = "enum " .. get_cname(type.name), lua_type = get_luaname(type.name), enumeration = type }
end

-- hack to avoid errors when assigning `palette.x = '#rrggbb'
-- TODO: Extend spec to allow optional types and either handle that in C or in lua adapters
type_lookup['rgb_color'].lua_type = type_lookup['rgb_color'].lua_type .. '|string'

local function lua_type(t)
  if t == nil then return "nil" end
  return type_lookup[t].lua_type
end


local function c_type(t)
  local entry = type_lookup[t] or error("Unrecognized type: " .. t)
  return entry.c_type
end

local function lua_check(t, idx)
  return type_lookup[t].check(idx)
end

local function lua_push(t, var)
  if not type_lookup[t].push then error(("lua type '%s' not defined."):format(var)) end
  return type_lookup[t].push(var)
end

-- recursively marshal a C struct into a lua table
-- The marshalling code is written as a string to tbl
--- @param tbl table
--- @param type string
--- @param path string
local function push_field(tbl, type, path)
  local tp = type_lookup[type]
  if tp.composite then
    table.insert(tbl, ([[
  lua_newtable(L); /* %s = new %s */
]]):format(path, tp.composite.name))
    for _, mem in ipairs(tp.composite.fields) do
      local mem_path = path .. "." .. mem.name
      if mem.optional then mem_path = mem_path .. '.value' end
      push_field(tbl, mem.type, mem_path)
      table.insert(tbl, ([[
  lua_setfield(L, -2, "%s"); /* %s = %s */
]]):format(mem.name, mem_path, mem.name))
    end
  elseif tp.enumeration then
    if tp.enumeration.flags then 
      table.insert(tbl, ([[
  lua_newtable(L); /* %s = %s flags */
]]):format(path, tp.enumeration.name))
      local enum_name = get_cname(type)

      for _, flag in ipairs(tp.enumeration.values) do
        local flag_name = enum_value_c_name(enum_name, flag.name)
        table.insert(tbl, ([[
  if (%s & %s) {
    lua_pushslice(L, %s_to_slice(%s));
    lua_pushboolean(L, true);
    lua_settable(L, -3);
  }
]]):format(path, flag_name, type, flag_name, tp.enumeration.name))
      end
    else
      table.insert(tbl, ([[
  lua_pushslice(L, %s_to_slice(%s));
]]):format(tp.enumeration.name, path))
    end
  else
    table.insert(tbl, ([[
  %s;
]]):format(lua_push(type, path)))
  end
end

--- @param tbl table
--- @param type_name string
--- @param path string
--- @param indent integer
local function check_field(tbl, type_name, path, indent)
  local result = {}
  indent = indent or 2
  local type = type_lookup[type_name]
  if type and type.composite then
    if not type.optional then
      table.insert(result, [[
luaL_checktype(L, -1, LUA_TTABLE);
]])
    end
    for _, mem in ipairs(type.composite.fields) do
      local mem_path = path .. "." .. mem.name
      table.insert(result, ([[
lua_getfield(L, -1, "%s"); /* get %s */
]]):format(mem.name, mem_path))
      if mem.optional then
        table.insert(result, ([[
if (!lua_isnoneornil(L, -1)) {
  %s.set = true;
]]):format(mem_path))
        check_field(result, mem.type, mem_path .. '.value', 2)
        table.insert(result, '}\n')
      else
        check_field(result, mem.type, mem_path, 0)
      end
      table.insert(result, ([[
lua_pop(L, 1); /* pop %s */
]]):format(mem_path))
    end
  elseif type and type.enumeration then
    if type.enumeration.flags then 
      local enum_name = get_cname(type.enumeration.name)
    table.insert(result, [[
luaL_checktype(L, -1, LUA_TTABLE);
]])
      for _, flag in ipairs(type.enumeration.values) do
        local flag_name = enum_value_c_name(enum_name, flag.name)
        table.insert(result, ([[
lua_getfield(L, -1, "%s");
if (!lua_isnoneornil(L, -1) && luaL_checkboolean(L, -1)) 
  %s |= %s;
lua_pop(L, 1);
]]):format(flag.name, path, flag_name))
      end
    else
      local varname = path:gsub('[.]', '_') .. '_str'
      table.insert(result, ([[
struct u8_slice %s = luaL_checkslice(L, -1);
%s = %s_slice_to_enum(%s);
if (%s == 0xffffffff) { 
  lua_pushstring(L, " is not a valid %s value.");
  lua_concat(L, 2);
  lua_error(L); 
}
]]):format(varname, path, type_name, varname, path, type_name))
    end
  else
    table.insert(result, ([[
%s = %s;
]]):format(path, lua_check(type_name, -1)))
  end
  local ident = string.rep(' ', indent)
  for _, str in ipairs(result) do
    for line in str:gmatch("[^\r\n]+") do
      table.insert(tbl, ident .. line .. '\n')
    end
  end
end

-- C Emitters {{{1

-- C Header {{{2
local h = {}
table.insert(h, ([[
/***************************************************
************ DO NOT EDIT THIS BY HAND **************
*** This file was auto generated by api_gen.lua ****
***************************************************/

#ifndef %s
#define %s

#include "lua.h"
#include <stdbool.h>
#include "utils.h"
#include "collections.h"

typedef lua_Integer lua_stackIndex;
typedef lua_Integer lua_stackRetCount;
struct velvet;

]]):format("VELVET_API_H", "VELVET_API_H"))

--- C enums {{{3
for _, enum in ipairs(spec.enums) do
  local cname = get_cname(enum.name)
  table.insert(h, ([[
enum %s {
]]):format(cname))
  for _, v in ipairs(enum.values) do
    local field_name = ("%s_%s"):format(cname, v.name):upper()
    table.insert(h, ([[
  %s = %d,
]]):format(field_name, v.value))
  end
  table.insert(h, [[
};

]])
end

--- C structs {{{3
-- Create structs for composite types.
-- These structs will automatically be marshaled to and from lua
for _, type in ipairs(spec.types) do
  local cname = get_cname(type.name)
  table.insert(h, ([[
struct %s {
]]):format(cname))
  for _, fld in ipairs(type.fields) do
    if fld.optional then
      table.insert(h, ([[
  struct {
    %s value;
    bool set;
  } %s; /* %s */
]]):format(c_type(fld.type), fld.name, string_concatenate(fld.doc, "")))
      else
    table.insert(h, ([[
  %s %s; /* %s */
]]):format(c_type(fld.type), fld.name, string_concatenate(fld.doc, "")))
    end
  end
  table.insert(h, [[
};

]])
end

for _, fn in ipairs(spec.api) do
  local required = {}
  local manual_return = is_manual(fn.returns.type)
  local has_manual_param = false
  for _, p in ipairs(fn.params or {}) do
    if is_manual(p.type) then
      has_manual_param = true
      table.insert(required, "lua_stackIndex " .. p.name)
    else
      table.insert(required, c_type(p.type) .. " " .. p.name)
    end
  end

  local required_params = #required > 0 and ", " .. table.concat(required, ", ") or ""

  table.insert(h, ("/* %s */\n"):format(string_concatenate(fn.doc, "\n** ")))
  if has_manual_param or manual_return then
    table.insert(h, ([[lua_stackRetCount vv_api_%s(lua_State *L%s);
]]):format(fn.name, required_params))
  else
    table.insert(h,
      ("%s vv_api_%s(struct velvet *v%s);\n")
      :format(c_type(fn.returns.type), fn.name, required_params)
    )
  end
end

for _, evt in ipairs(spec.events) do
  local event_name = evt.name:gsub("[.]", "_")
  local event_arg_name = get_cname(evt.args)
  table.insert(h, ([=[
/* %s */
void velvet_api_raise_%s(struct velvet *v, __attribute__((unused)) struct %s args);
]=]):format(evt.doc, event_name, event_arg_name))
end

table.insert(h, ("#endif /* %s */\n"):format("VELVET_API_H"))

write_file(out_dir .. "/velvet_api.h", table.concat(h))

-- C Lua Marshalling {{{2

-- C Lua Functions {{{3

local c = {}
table.insert(c, [[
/***************************************************
************ DO NOT EDIT THIS BY HAND **************
*** This file was auto generated by api_gen.lua ****
***************************************************/

#include "lua.h"
#include "lauxlib.h"
#include "velvet_api.h"
#include "velvet_lua.h"
#include "velvet.h"
#include "utils.h"
#include <string.h>

]])

for _, enum in ipairs(spec.enums) do
  local cname = get_cname(enum.name)
  -- String to integer value {{{4
  table.insert(c, ([=[
__attribute__((unused)) static %s %s_slice_to_enum(struct u8_slice str) {
]=]):format(c_type(enum.name), enum.name))
  for _, option in ipairs(enum.values) do
    local field_name = enum_value_c_name(cname, option.name)
    table.insert(c, ([[
  if (u8_slice_equals(str, (struct u8_slice) { .content = (const uint8_t*)"%s", .len = %d })) return %s;
]]):format(option.name, #option.name, field_name))
  end
  table.insert(c, '  return 0xffffffff;\n}\n\n')

  -- Integer value to string {{{4
  table.insert(c, ([=[
__attribute__((unused)) static struct u8_slice %s_to_slice(%s value) {
]=]):format(enum.name, c_type(enum.name)))
  table.insert(c, [[
  switch (value) {
]])
  for _, option in ipairs(enum.values) do
    local field_name = ("%s_%s"):format(cname, option.name):upper()
    table.insert(c, ([[
  case %s: return (struct u8_slice) { .content = (const uint8_t*)"%s", .len = %d };
]]):format(field_name, option.name, #option.name))
  end
  table.insert(c, ([[
  default: assert(!"%s value out of range");
]]):format(enum.name));
  table.insert(c, '  };\n')
  table.insert(c, [[
]])
  table.insert(c, '}\n\n')
end

-- C API function marshalling {{{3

for _, fn in ipairs(spec.api) do
  local manual_return = is_manual(fn.returns.type)
  local has_manual_param = false

  for _, p in ipairs(fn.params or {}) do
    if is_manual(p.type) then
      has_manual_param = true
      break
    end
  end

  table.insert(c, ([[

static int l_vv_api_%s(lua_State *L) {
]])
    :format(fn.name))
  if not has_manual_param and not manual_return then
    table.insert(c, [[
  struct velvet *v = *(struct velvet **)lua_getextraspace(L);
  v->current = L;
]])
  end

  local idx = 1
  local args = {}
  for _, p in ipairs(fn.params or {}) do
    local t = type_lookup[p.type]
    if is_manual(p.type) then
      -- pass the stack index; the implementation reads it from L
      table.insert(c,
        ("  lua_Integer %s = %d;\n"):format(p.name, idx)
      )
      table.insert(args, p.name)
      idx = idx + 1
    elseif t.composite then
      if not t.optional then
        table.insert(c, ([[
  luaL_checktype(L, %d, LUA_TTABLE);
]]):format(idx))
      end
      table.insert(c, ([[
  %s %s = {0};
  if (!lua_isnoneornil(L, %d)) {
    luaL_checktype(L, %d, LUA_TTABLE);
    lua_pushvalue(L, %d); /* push table to the top of the stack */
]]):format(c_type(p.type), p.name, idx, idx, idx))

      check_field(c, p.type, p.name, 4)
      table.insert(c, [[
    lua_pop(L, 1); /* pop pushed table */
  }
]])
      table.insert(args, p.name)
      idx = idx + 1
    elseif t and t.enumeration then
      table.insert(c, ([[
  struct u8_slice %s_str = %s;
  %s %s = %s_slice_to_enum(%s_str);
]]):format(p.name, lua_check('string', idx), c_type(p.type), p.name, p.type, p.name))
      table.insert(args, p.name)
      idx = idx + 1
    else
      table.insert(c,
        ("  %s %s = %s;\n")
          :format(c_type(p.type), p.name, lua_check(p.type, idx))
      )
      table.insert(args, p.name)
      idx = idx + 1
    end
  end

  local argsstring = #args > 0 and ", " .. table.concat(args, ", ") or ""
  local t = type_lookup[fn.returns.type]
  if has_manual_param or manual_return then
    table.insert(c, ([[
  return vv_api_%s(L%s);
}
]]):format(fn.name, argsstring))
  elseif fn.returns.type == 'void' then
    table.insert(c, ([[
  vv_api_%s(v%s);
  return 0;
}
]]):format(fn.name, argsstring))
  else
    table.insert(c, ([[
  %s ret = vv_api_%s(v%s);
]]):format(c_type(fn.returns.type), fn.name, argsstring))
    push_field(c, fn.returns.type, "ret")
    table.insert(c, [[
  return 1;
}
]])
  end
end

-- Generate lua function table {{{3

table.insert(c, [=[
__attribute__((unused)) static const struct luaL_Reg velvet_lua_function_table[] = {
]=])

for _, fn in ipairs(spec.options) do
  local name = fn.name
  table.insert(c, ([[
  { "get_%s", l_vv_api_get_%s },
  { "set_%s", l_vv_api_set_%s },
]]):format(name, name, name, name))
end

for _, fn in ipairs(spec.api) do
  local name = fn.name
  table.insert(c, ([[
  { "%s", l_vv_api_%s },
]]):format(name, name))
end


table.insert(c, [[
  {0} /* sentinel */
};
]])

-- Event marshalling {{{3

for _, evt in ipairs(spec.events) do
  local event_name = evt.name:gsub("[.]", "_")
  local event_arg_name = get_cname(evt.args)
  table.insert(c, ([=[

void velvet_api_raise_%s(struct velvet *v, __attribute__((unused)) struct %s args) {
  lua_State *L = v->L;
  if (!L) return;
  lua_getglobal(L, "vv");
  lua_getfield(L, -1, "events");
  lua_getfield(L, -1, "emit");
  lua_pushlstring(L, "%s", %d); /* event name */
]=]):format(event_name, event_arg_name, evt.name, #evt.name))

  push_field(c, evt.args, "args")

  table.insert(c, [[
  /* vv.events.emit(args) */
  if (lua_pcall(L, 2, 0, 0) != LUA_OK) { 
    const char *err = lua_tostring(L, -1);
    velvet_log("lua emit: %s", err);
    lua_pop(L, 1);
  }
  lua_pop(L, 2);
}
]])
end

-- Write lua_autogen.c {{{3

write_file(out_dir .. "/velvet_lua_autogen.c", table.concat(c))



-- LUA emitters {{{1

-- _api.lua {{{2
local lua = {}

table.insert(lua, [=[
--[[
DO NOT EDIT THIS BY HAND
This file was auto generated by api_gen.lua
--]]

error("Cannot require meta file")

--- @meta
--- @class velvet.api
local api = {}
]=])

-- Generate enum specs {{{3

for _, enum in ipairs(spec.enums) do
  local lua_name = get_luaname(enum.name)
  table.insert(lua, ([[
---@alias %s string %s
]]):format(lua_name, enum.doc or ""))
  for _, v in ipairs(enum.values) do
    table.insert(lua, ([[
---| '%s' %s
]]):format(v.name, v.doc or ""))
  end
  if enum.flags then
    table.insert(lua, ([[

--- @class %ss Flags for %s
]]):format(lua_name, lua_name))
    for _, value in ipairs(enum.values) do
      table.insert(lua, ([[
--- @field %s? boolean %s
]]):format(value.name, value.doc or ""))
    end
  end
  table.insert(lua, '\n')
end

-- Generate type definitions for composite types {{{3

for _, type in ipairs(spec.types) do
  local lua_name = get_luaname(type.name)
  table.insert(lua, ([[

--- @class %s
]]):format(lua_name))
  for _, fld in ipairs(type.fields) do
    local lt = lua_type(fld.type) .. (fld.alias and ('|velvet.api.' .. fld.alias) or '')
    local t = type_lookup[fld.type]
    if t.enumeration and t.enumeration.flags then
      lt = ('%ss'):format(lt)
    end
    table.insert(lua, ([[
--- @field %s%s %s %s
]]):format(fld.name, fld.optional and '?' or '', lt, fld.doc))
  end
end

-- Generate api function spec {{{3

for _, fn in ipairs(spec.api) do
  table.insert(lua, ([[

--- %s
]]):format(string_concatenate(fn.doc, "\n--- ")))
  for _, p in ipairs(fn.params or {}) do
    local t = type_lookup[p.type]
    table.insert(lua, ([[
--- @param %s%s %s %s
]]):format(p.name, t.optional and '?' or '', lua_type(p.type), string_concatenate(p.doc, "\n--- ")))
  end

  local params = {}
  for _, p in ipairs(fn.params or {}) do
    table.insert(params, p.name)
  end
  table.insert(lua, ([[
--- @return %s ret %s
]]):format(lua_type(fn.returns.type), string_concatenate(fn.returns.doc, "\n--- ")))

  table.insert(lua,
    ("function api.%s(%s) end\n")
    :format(fn.name, table.concat(params, ", "))
  )
end

-- Stub out event handlers {{{3

table.insert(lua, [[

--- @class velvet.api.event_handler
--- @field name string The name of the handler
--- @field id integer The id of the handler
]])
for _, evt in ipairs(spec.events) do
  table.insert(lua, ([[
--- @field %s? fun(event_args: velvet.api.%s): nil %s
]]):format(evt.name:gsub('[.]', '_'), evt.args, evt.doc))
end

-- Write _api.lua {{{3

table.insert(lua, "\n")
write_file("lua/velvet/_api.lua", table.concat(lua))

-- _options.lua {{{2

local options = {}
table.insert(options, [[
error("Cannot require meta file")
--- @meta
--- @class velvet.options
local options = {}
]])

for _, fn in ipairs(spec.options) do
  local luatype = lua_type(fn.type)
  table.insert(options, ([[
--- %s
--- @type %s
options.%s = %s

]]):format(string_concatenate(fn.doc, "\n--- "), luatype, fn.name, inspect(fn.default)))
end

table.insert(options, "return options\n")
write_file("lua/velvet/_options.lua", table.concat(options))

-- generate options.lua {{{2

local default_options = {}

table.insert(default_options, [[
--- DO NOT EDIT THIS BY HAND
--- This file was auto generated by api_gen.lua
--- It sets all options to their default values.

]])

--- Set default options {{{3

for _, fn in ipairs(spec.options) do
  table.insert(default_options, ([[
vv.options.%s = %s
]]):format(fn.name, inspect(fn.default)))
end


--- write file {{{3

table.insert(default_options, "\n")
write_file("lua/velvet/default_options.lua", table.concat(default_options))

--- generate async.lua {{{2
local async = {}
table.insert(async, [==[
--[[
DO NOT EDIT THIS BY HAND
This file was auto generated by api_gen.lua
--]]

]==])

--- Description of known events {{{3
table.insert(async, [[

--- The async API is a coroutine based implementation of velvet's event system, enabling linear control flow.
local M = {}

local e = require('velvet.events').create_group('velvet.async', true)
local registered_waits = {}
local sequence_callbacks = {}
local co_to_seq = {}
local co_defer = {}
local deferring = {}
-- Monotonically increasing sequence number used to invalidate multi-waits
local sequence = 1

local known_events = {
]])
for _, evt in ipairs(spec.events) do
  table.insert(async, ("  [ [[%s]] ] = [[%s]],\n"):format(evt.name, evt.doc))
end

table.insert(async, [[
}

--- @return table<string, string|boolean> seen known events
function M.get_observed_events()
  return vv.deepcopy(known_events)
end
]])

--- coroutine dispatching machinery {{{3
table.insert(async, ([[

--- Resolve all defers for |co|
--- This is called when |co| completes.
local function exec_defer(co)
  local defer = co_defer[co]
  if defer then
    -- ensure no new wait() and defer() calls are made on this thread during defer
    deferring[co] = true
    -- Ensure co_defer is nilled in case a defer calls M.cancel()
    -- Further defer calls will now error().
    co_defer[co] = nil
    for i = #defer, 1, -1 do
      local fn = defer[i]
      local ok, err = xpcall(fn, debug.traceback)
      if not ok then
        printerr(("Unhandled error in coroutine defer: %s"):format(err), 'error')
      end
    end
    deferring[co] = nil
  end
end

--- Execute |f| as a coroutine.
--- @param f fun(...): any
--- @param ... any arguments passed to f
--- @return thread co the coroutine executing |f|. Can be cancelled with M.cancel()
function M.run(f, ...)
  local args = table.pack(...)
  local co = coroutine.create(function()
    co_defer[coroutine.running()] = {}
    local ok, err = xpcall(f, debug.traceback, table.unpack(args, 1, args.n))
    if not ok then
      printerr(("Unhandled error in coroutine: %s"):format(err), 'error')
    end
    exec_defer(coroutine.running())
  end)
  coroutine.resume(co)
  return co
end

--- Cancel all continuations for |co| and trigger deferred actions.
--- @param co thread the thread to cancel
function M.cancel(co)
  local seq = co_to_seq[co]
  if seq then
    co_to_seq[co] = nil
    sequence_callbacks[seq] = nil
  end
  exec_defer(co)
end

--- defer a function which runs when the current coroutine completes or is cancelled.
--- @param defer fun() deferred action
function M.defer(defer)
  if deferring[coroutine.running()] then error("Cannot add new defers during defer.") end
  assert(type(defer) == 'function', string.format('Bad argument #1 (function expected, got %s)', type(defer)))
  local defers = co_defer[coroutine.running()] or error("Provided coroutine is not managed by vv.async.")
  defers[#defers + 1] = defer
end

local function resolve(name, data)
  local current_sequence = sequence
  local function resolve_table(tbl)
    -- capture the current sequence number and ensure we don't resolve anything higher.
    -- Otherwise a waiter() invocation can trigger on the currently processing event.
    for seq, registration in pairs(tbl) do
      if type(seq) == 'number' then
        if seq <= current_sequence then
          local is_match = true
          local wait_result = { name = name, data = data }
          if registration.when then 
            local ok, result = xpcall(registration.when, debug.traceback, registration, wait_result)
            if not ok then
              printerr(string.format("Unhandled error during when(%s): %s", name, result))
              return
            else
              is_match = result
            end
          end
          if is_match then
            local waiter = sequence_callbacks[seq]
            if waiter then
              sequence_callbacks[seq] = nil
              waiter(registration, wait_result)
            end
            tbl[seq] = nil
          end
        end
      end
    end
  end

  if not known_events[name] then known_events[name] = true end
  local segments = {}
  for segment in name:gmatch('[^.]+') do
    segments[#segments+1] = segment
  end

  local function recursive_resolve(level, word, ...)
    local leaf = select('#', ...) == 0
    local any = level['**']
    if any then resolve_table(any) end
    local star = level['*']
    local match = level[word]

    if leaf then
      if star then resolve_table(star) end
      if match then resolve_table(match) end
    else
      if star then recursive_resolve(star, ...) end
      if match then recursive_resolve(match, ...) end
    end
  end

  recursive_resolve(registered_waits, table.unpack(segments))
end

e['**'] = resolve
]]))

--- Event name type alias {{{3
table.insert(async, [[

--- @alias velvet.async.event
]])
for _, evt in ipairs(spec.events) do
  table.insert(async, ("---| '%s' %s\n"):format(evt.name, evt.doc))
end

table.insert(async, [[

--- @class velvet.async.conditional_event
--- @field event velvet.async.event|string event
--- @field when fun(registration: velvet.async.event_registration, result: velvet.async.wait.result): boolean predicate function

--- @alias velvet.async.event_registration velvet.async.event|velvet.async.conditional_event|'*'|'**'|string

--- @class velvet.async.wait.result
--- @field name velvet.async.event|string the name of the raised event
--- @field data any the event args

--- Wait for one of the events to fire, or |timeout|.
--- @param ... velvet.async.event_registration|integer One or more events to wait for. A number can optionally be parsed which will be interpreted as the timeout in milliseconds.
--- @return velvet.async.event_registration, velvet.async.wait.result The argument which resolved the wait, and the wait result, or 'timeout' on timeout
function M.wait(...)
  local timeout = nil
  local co = coroutine.running()
  if deferring[co] then error("Cannot wait() during defer.") end
  sequence = sequence + 1
  -- local capture to preserve the sequence number
  local seq = sequence

  local args = {...}
  if #args == 0 then error("No events specified.") end
  for i, evt in ipairs(args) do
    if type(evt) ~= 'number' and type(evt) ~= 'string' and type(evt) ~= 'table' then
      error(("Bad argument #%d (number, string or table expected)"):format(i))
    end
  end

  co_to_seq[co] = seq
  sequence_callbacks[seq] = function(registration, result)
    if timeout then vv.api.schedule_cancel(timeout) end
    local ok, error = coroutine.resume(co, registration, result)
    if not ok then
      printerr(string.format("Unhandled error in coroutine after %s: %s", result.name, debug.traceback(error, 0)))
    end
  end

  for idx, evt in ipairs(args) do
    if type(evt) == 'number' then
      timeout = vv.api.schedule_after(evt, function()
        -- if sequence_callbacks was unset, that means this coroutine was cancelled.
        if not sequence_callbacks[seq] then return end
        sequence_callbacks[seq] = nil
        local ok, error = coroutine.resume(co, nil, 'timeout')
        if not ok then
          printerr(string.format("Unhandled error in coroutine after timeout: %s", result.name, debug.traceback(error, 0)))
        end
      end)
    elseif type(evt) == 'string' or type(evt) == 'table' then
      local event = evt
      if type(evt) == 'table' then
        assert(type(evt.event) == 'string', ("Bad argument #%d: bad field 'event' (string expected, got %s)"):format(idx, type(evt.event)))
        if evt.when ~= nil then
          assert(type(evt.when) == 'function', ("Bad argument #%d: bad field 'when' (function expected, got %s)"):format(idx, type(evt.when)))
        end
        event = evt.event
      end
      assert(type(event) == 'string')
      local tbl = registered_waits
      for segment in event:gmatch('[^.]+') do
        local sub = tbl[segment]
        if not sub then
          sub = {}; tbl[segment] = sub
        end
        tbl = sub
      end
      if tbl == registered_waits then 
        error(('Bad argument #%d (malformed event specifier %s)'):format(idx, event))
      end
      tbl[seq] = evt
    else 
      error(('bad argument #%d (string|number expected, got %s)'):format(idx, type(evt)))
    end
  end

  return coroutine.yield()
end

--- Returns an iterator which yields whenever an event in |...| is fired. Terminates on timeout if specified.
--- @param ... velvet.async.event_registration|integer One or more events to stream. A number can optionally be parsed which will be interpreted as the timeout in milliseconds.
--- @return fun(): velvet.async.event_registration?, velvet.async.wait.result? Iterator which streams the input events
function M.stream(...)
  local args = {...}
  return function()
    local ok, result = M.wait(table.unpack(args))
    return ok or 'timeout', result
  end
end

]])

--- Generate user-facing API {{{3
for _, evt in ipairs(spec.events) do
  table.insert(async, ([[

--- Wait for %s
--- @param timeout? integer Optional timeout.
--- @param when? fun(event: string, data: velvet.api.%s): boolean predicate function
--- @return velvet.api.%s ret Result, or nil on timeout.
function M.wait_for_%s(timeout, when)
  local event = '%s'
  local registration = when and { event = event, when = when } or event
  local _, result = M.wait(registration, timeout)
  return result.data
end
]]):format(evt.name, evt.args, evt.args, evt.name:gsub('[.]', '_'), evt.name))
end

table.insert(async, "return M")

--- write file {{{3
table.insert(async, "\n")
write_file("lua/velvet/async.lua", table.concat(async))

-- Modeline {{{1
-- vim: fdm=marker shiftwidth=2
