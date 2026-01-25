-- Setup {{{1
local spec_path = assert(arg[1], "usage: lua api_gen.lua <api_spec.lua> <out_dir>")
local out_dir = assert(arg[2], "usage: lua api_gen.lua <api_spec.lua> <out_dir>")

--- @type spec
local spec = dofile(spec_path)

local inspect = dofile("lua/velvet/inspect.lua")
local dbg = function(x) print(inspect(x)) end

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
  fn.optional = fn.optional or {}
  fn.returns = fn.returns or { type = "void" }

  for _, p in ipairs(fn.params) do
    p.doc = string_lines(p.doc)
  end
  for _, o in ipairs(fn.optional) do
    o.doc = string_lines(o.doc)
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
  ["int[]"] = { lua_type = "integer[]", c_type = "int[]" },
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
    c_type = "const char*",
    lua_type = "string",
    check = function(idx) return ("luaL_checklstring(L, %d, NULL)"):format(idx) end,
    push = function(var) return ("lua_pushstring(L, %s)"):format(var) end
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
  local manual_types = { ["int[]"] = true }
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
  lua_newtable(L); /* %s */
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
  lua_newtable(L); /* %s flags */
]]):format(path, tp.enumeration.name, tp.enumeration.name))
      local enum_name = get_cname(type)

      for _, flag in ipairs(tp.enumeration.values) do
        local flag_name = enum_value_c_name(enum_name, flag.name)
        table.insert(tbl, ([[
  if (%s & %s) {
    lua_pushstring(L, %s_to_string(%s));
    lua_pushboolean(L, true);
    lua_settable(L, -3);
  }
]]):format(path, flag_name, type, flag_name, tp.enumeration.name))
      end
    else
      table.insert(tbl, ([[
  lua_pushstring(L, %s_to_string(%s));
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
const char *%s = luaL_checklstring(L, -1, NULL);
%s = %s_string_to_enum(%s);
if (%s == 0xffffff) { 
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
  for _, p in ipairs(fn.params or {}) do
    table.insert(required, c_type(p.type) .. " " .. p.name)
  end

  local optional = {}
  for _, o in ipairs(fn.optional or {}) do
    table.insert(optional, c_type(o.type) .. "* " .. o.name)
  end

  local required_params = #required > 0 and ", " .. table.concat(required, ", ") or ""
  local optional_params = #optional > 0 and ", " .. table.concat(optional, ", ") or ""

  table.insert(h, ("/* %s */\n"):format(string_concatenate(fn.doc, "\n** ")))
  if is_manual(fn.returns.type) then
    table.insert(h, ([[lua_Integer vv_api_%s(lua_State *L%s%s);
]]):format(fn.name, required_params, optional_params))
  else
    table.insert(h,
      ("%s vv_api_%s(struct velvet *v%s%s);\n")
      :format(c_type(fn.returns.type), fn.name, required_params, optional_params)
    )
  end
end

for _, evt in ipairs(spec.events) do
  local event_name = evt.name:gsub("[.]", "_")
  local event_arg_name = get_cname(evt.args)
  table.insert(h, ([[
/* %s */
void velvet_api_raise_%s(struct velvet *v, struct %s args);
]]):format(evt.doc, event_name, event_arg_name))
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
#include "velvet.h"
#include "utils.h"
#include <string.h>

/* Instead of creating a real gc handle, we only store a reference to the function.
Cleaning up the handle in codegen is complicated, so instead the consumer must create its own handle. */
static lua_Integer luaL_checkfunction(lua_State *L, lua_Integer idx) {
  luaL_checktype(L, idx, LUA_TFUNCTION);
  return idx;
}

static bool luaL_checkboolean(lua_State *L, lua_Integer idx) {
  luaL_checktype(L, idx, LUA_TBOOLEAN);
  return lua_toboolean(L, idx);
}

]])

for _, enum in ipairs(spec.enums) do
  local cname = get_cname(enum.name)
  -- String to integer value {{{4
  table.insert(c, ([[
static %s %s_string_to_enum(const char *str) {
]]):format(c_type(enum.name), enum.name))
  for _, option in ipairs(enum.values) do
    local field_name = enum_value_c_name(cname, option.name)
    table.insert(c, ([[
  if (strcmp(str, "%s") == 0) return %s;
]]):format(option.name, field_name))
  end
  table.insert(c, '  return 0xffffffff;\n};\n\n')

  -- Integer value to string {{{4
  local table_name = enum.name .. "_idx_to_string"
  table.insert(c, ([[
static const char *%s_to_string(%s value) {
]]):format(enum.name, c_type(enum.name)))
  table.insert(c, ([[
  switch (value) {
]]):format(table_name))
  for _, option in ipairs(enum.values) do
    local field_name = ("%s_%s"):format(cname, option.name):upper()
    table.insert(c, ([[
  case %s: return "%s";
]]):format(field_name, option.name))
  end
  table.insert(c, ([[
  default: assert(!"%s value out of range");
]]):format(enum.name));
  table.insert(c, '  };\n')
  table.insert(c, ([[
]]):format(table_name, table_name, table_name))
  table.insert(c, '}\n\n')
end

-- C API function marshalling {{{3

for _, fn in ipairs(spec.api) do
  local params = {}
  for _, p in ipairs(fn.params or {}) do
    table.insert(params, c_type(p.type) .. " " .. p.name)
  end

  table.insert(c, ([[

static int l_vv_api_%s(lua_State *L) {
]])
    :format(fn.name))
  if not is_manual(fn.returns.type) then
    table.insert(c, '  struct velvet *v = *(struct velvet **)lua_getextraspace(L);\n')
  end

  local idx = 1
  local args = {}
  for _, p in ipairs(fn.params or {}) do
    local t = type_lookup[p.type]
    if t.composite then
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
    elseif t and t.enumeration then
      table.insert(c, ([[
  const char *%s_str = %s;
  %s %s = %s_string_to_enum(%s_str);
]]):format(p.name, lua_check('string', idx), c_type(p.type), p.name, p.type, p.name))
    else
      table.insert(c,
        ("  %s %s = %s;\n")
        :format(c_type(p.type), p.name, lua_check(p.type, idx))
      )
    end
    table.insert(args, p.name)
    idx = idx + 1
  end

  if #fn.optional > 0 then
    for _, o in ipairs(fn.optional) do
      table.insert(c, ([[
  %s %s;
  %s* p_%s = NULL;
]]):format(c_type(o.type), o.name, c_type(o.type), o.name))
      table.insert(args, "p_" .. o.name)
    end

    table.insert(c, ([[
  if (!lua_isnoneornil(L, %d)) {
    luaL_checktype(L, %d, LUA_TTABLE);
]]):format(idx, idx, idx))

    for _, o in ipairs(fn.optional) do
      table.insert(c, ([[
    lua_getfield(L, %d, "%s");
    if (!lua_isnil(L, -1)) {
      %s = %s;
      p_%s = &%s;
    }
    lua_pop(L, 1);
]]):format(idx, o.name, o.name, lua_check(o.type, -1), o.name, o.name))
    end

    table.insert(c, "  }\n")
  end

  local argsstring = #args > 0 and ", " .. table.concat(args, ", ") or ""
  local t = type_lookup[fn.returns.type]
  if fn.returns.type == 'void' then
    table.insert(c, ([[
  vv_api_%s(v%s);
  return 0;
}
]]):format(fn.name, argsstring))
  elseif is_manual(fn.returns.type) then 
    table.insert(c, ([[
  return vv_api_%s(L%s);
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
]]);
  end
end

-- Generate lua function table {{{3

table.insert(c, [=[
[[maybe_unused]] static const struct luaL_Reg velvet_lua_function_table[] = {
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
  table.insert(c, ([[

void velvet_api_raise_%s(struct velvet *v, struct %s args) {
  lua_State *L = v->L;
  lua_getglobal(L, "vv");
  lua_getfield(L, -1, "events");
  lua_getfield(L, -1, "emit_event");
  lua_pushstring(L, "%s"); /* event name */
]]):format(event_name, event_arg_name, evt.name))

  push_field(c, evt.args, "args")

  table.insert(c, [[
  /* vv.events.emit_event(args) */
  if (lua_pcall(L, 2, 0, 0) != LUA_OK) { 
    const char *err = lua_tostring(L, -1);
    velvet_log("lua emit: %s", err);
    lua_pop(L, 1);
  }
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
    local lt = lua_type(fld.type)
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
  local optional = fn.optional or {}
  if #optional > 0 then
    table.insert(lua, ([[

--- @class velvet.api.%s.Opts
]]):format(fn.name))
    for _, opt in ipairs(optional) do
      table.insert(lua, ([[
--- @field %s? %s %s
]]):format(opt.name, lua_type(opt.type), string_concatenate(opt.doc, "\n--- ")))
    end
  end
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
  if #optional > 0 then
    table.insert(lua, ([[
--- @param opts? velvet.api.%s.Opts
]]):format(fn.name))
    table.insert(params, "opts")
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
]]):format(evt.name, evt.args, evt.doc))
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


-- Modeline {{{1
-- vim: fdm=marker shiftwidth=2
