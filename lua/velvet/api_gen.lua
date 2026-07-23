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


local function string_replace(template_string, template_args)
  local count = nil
  for k, v in pairs(template_args or {}) do
    local pat = '<' .. k .. '>'
    template_string, count = template_string:gsub(pat, v)
    if count == 0 then error("Template argument " .. k .. " had no matches.") end
  end
  local unresolved = template_string:match("<%w+>")
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

local function get_cname(name)
  return "velvet_api_" .. name:gsub("%.", "_")
end

local function get_luaname(name)
  return "velvet.api." .. name
end

local function enum_value_c_name(enum, option)
  return ("%s_%s"):format(enum, option):upper()
end

local function is_manual(name)
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

for _, cb in ipairs(spec.callbacks) do
  local args = {}
  for _, arg in ipairs(cb.params) do
    local template = { name = arg.name, type = arg.type .. (arg.optional and '|nil' or '') }
    args[#args + 1] = string_replace("<name>: <type>", template)
  end
  local template = { args = table.concat(args, ", "), ret = cb.returns and cb.returns.type or 'nil'}
  local signature = string_replace("fun(<args>): <ret>", template)
  local entry = { c_type = "lua_Integer", lua_type = signature, check_shift = check_function_and_shift }
  type_lookup[cb.name] = entry
end

for _, type in ipairs(spec.enums) do
  type_lookup[type.name] = {
    c_type = "enum " .. get_cname(type.name),
    lua_type = get_luaname(type.name),
    enumeration =
        type
  }
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
  if not entry.c_type then error('Cannot handle type: ' .. t) end
  return entry.c_type
end

local function lua_check(t, idx, idx2)
  local tp = type_lookup[t]
  if idx2 and tp.check_shift then return tp.check_shift(idx, idx2) end
  return tp.check(idx)
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
    table_insert_template(tbl, [[
  lua_newtable(L); /* <path> = new <name> */
]], { path = path, name = tp.composite.name })
    for _, mem in ipairs(tp.composite.fields) do
      local mem_path = path .. "." .. mem.name
      if mem.optional then mem_path = mem_path .. '.value' end
      push_field(tbl, mem.type, mem_path)
      table_insert_template(tbl, [[
  lua_setfield(L, -2, "<name>"); /* <path> = <name> */
]], { name = mem.name, path = mem_path })
    end
  elseif tp.enumeration then
    if tp.enumeration.flags then
      table_insert_template(tbl, [[
  lua_newtable(L); /* <path> = <flag> flags */
]], { path = path, flag = tp.enumeration.name })
      local enum_name = get_cname(type)

      for _, flag in ipairs(tp.enumeration.values) do
        local flag_name = enum_value_c_name(enum_name, flag.name)
        table_insert_template(tbl, ([[
  if (<value> & <flag>) {
    lua_pushslice(L, <type>_to_slice(<flag>));
    lua_pushboolean(L, true);
    lua_settable(L, -3);
  }
]]), { value = path, flag = flag_name, type = type })
      end
    else
      table_insert_template(tbl, [[
  lua_pushslice(L, <name>_to_slice(<value>)); /* <3 */
]], { name = tp.enumeration.name, value = path })
    end
  else
    table_insert_template(tbl, [[
  <push>;
]], { push = lua_push(type, path) })
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
      table_insert_template(result, [[
luaL_checktype(L, -1, LUA_TTABLE);
]], {})
    end
    for _, mem in ipairs(type.composite.fields) do
      local mem_path = path .. "." .. mem.name
      table_insert_template(result, [[
lua_getfield(L, -1, "<name>"); /* get <path> */
]], { name = mem.name, path = mem_path })
      if mem.optional then
        table_insert_template(result, [[
if (!lua_isnoneornil(L, -1)) {
  <path>.set = true;
]], { path = mem_path })
        check_field(result, mem.type, mem_path .. '.value', 2)
        table_insert_template(result, '}\n')
      else
        check_field(result, mem.type, mem_path, 0)
      end
      table_insert_template(result, [[
lua_pop(L, 1); /* pop <path> */
]], { path = mem_path })
    end
  elseif type and type.enumeration then
    if type.enumeration.flags then
      local enum_name = get_cname(type.enumeration.name)
      table_insert_template(result, [[
luaL_checktype(L, -1, LUA_TTABLE);
]])
      for _, flag in ipairs(type.enumeration.values) do
        local c_name = enum_value_c_name(enum_name, flag.name)
        table_insert_template(result, [[
lua_getfield(L, -1, "<flag_name>");
if (!lua_isnoneornil(L, -1) && luaL_checkboolean(L, -1))
  <path> |= <c_name>;
lua_pop(L, 1);
]], { c_name = c_name, flag_name = flag.name, path = path })
      end
    else
      table_insert_template(result, [[
struct u8_slice <name> = luaL_checkslice(L, -1);
int <type>_conv = <type>_slice_to_enum(<name>);
if (<type>_conv == ~0) {
  lua_pushstring(L, " is not a valid <type> value.");
  lua_concat(L, 2);
  lua_error(L);
}
<path> = <type>_conv;
]], { name = path:gsub('%.', '_') .. '_str', type = type_name, path = path })
    end
  else
    table_insert_template(result, [[
<path> = <check>;
]], { path = path, check = lua_check(type_name, -1, "++argtop") })
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
local api_header = {}
table_insert_template(api_header, [[
/***************************************************
************ DO NOT EDIT THIS BY HAND **************
*** This file was auto generated by api_gen.lua ****
***************************************************/

#ifndef VELVET_API_H
#define VELVET_API_H

#include "lua.h"
#include <stdbool.h>
#include "collections.h"

typedef lua_Integer lua_stackIndex;
typedef lua_Integer lua_stackRetCount;
struct velvet;

]])

--- C enums {{{3
for _, enum in ipairs(spec.enums) do
  local cname = get_cname(enum.name)
  if enum.doc then table_insert_template(api_header, "/* <doc> */\n", { doc = enum.doc }) end
  table_insert_template(api_header, [[
enum <packed><name> {
]], { packed = enum.packed and "__attribute__((packed)) " or "", name = cname })
  for _, v in ipairs(enum.values) do
    if v.doc then table_insert_template(api_header, "  /* <doc> */\n", { doc = v.doc }) end
    local field_name = ("%s_%s"):format(cname, v.name):upper()
    table_insert_template(api_header, [[
  <name> = <value>,
]], { name = field_name, value = v.value })
  end
  table_insert_template(api_header, '\n};\n\n')
end

--- C structs {{{3
-- Create structs for composite types.
-- These structs will automatically be marshaled to and from lua
for _, type in ipairs(spec.types) do
  if is_manual(type.name) then
    goto continue
  end
  if type.doc then table_insert_template(api_header, "/* <doc> */\n", { doc = type.doc }) end
  local cname = get_cname(type.name)
  table_insert_template(api_header, [[
struct <name> {
]], { name = cname })
  for _, fld in ipairs(type.fields) do
    if fld.doc then table_insert_template(api_header, "  /* <doc> */\n", { doc = fld.doc }) end
    if fld.optional then
      table_insert_template(api_header, [[
  struct {
    <field_name> value;
    bool set;
  } <name>;
]], { field_name = c_type(fld.type), name = fld.name })
    else
      table_insert_template(api_header, [[
  <type> <name>;
]], { type = c_type(fld.type), name = fld.name })
    end
  end
  table_insert_template(api_header, '\n};\n\n')
  ::continue::
end

for _, evt in ipairs(spec.events) do
  local event_name = evt.name:gsub("%.", "_")
  local event_arg_name = get_cname(evt.args)
  table_insert_template(api_header, [[
/* <doc> */
void velvet_api_raise_<event>(struct velvet *v, __attribute__((unused)) struct <arg_type> args);
]], { doc = evt.doc, event = event_name, arg_type = event_arg_name })
end

table_insert_template(api_header, "#endif /* VELVET_API_H */\n")

write_file(out_dir .. "/velvet_api.h", table.concat(api_header))

-- C Lua Marshalling {{{2

-- Shared C helpers {{{3

local c_helpers = {}
table_insert_template(c_helpers, [[
/***************************************************
************ DO NOT EDIT THIS BY HAND **************
*** This file was auto generated by api_gen.lua ****
***************************************************/

#include "velvet_api.h"

]])

for _, enum in ipairs(spec.enums) do
  local cname = get_cname(enum.name)
  -- String to integer value {{{4
  table_insert_template(c_helpers, [=[
__attribute__((unused)) static int <enum>_slice_to_enum(struct u8_slice str) {
]=], { enum = enum.name })
  for _, option in ipairs(enum.values) do
    table_insert_template(c_helpers, [[
  if (u8_slice_equals(str, (struct u8_slice){.content = (const uint8_t *)"<option>", .len = <len>})) return <value>;
]], { option = option.name, len = #option.name, value = enum_value_c_name(cname, option.name) })
  end
  table_insert_template(c_helpers, '  return ~0;\n}\n\n')

  -- Integer value to string {{{4
  table_insert_template(c_helpers, [=[
__attribute__((unused)) static struct u8_slice <name>_to_slice(<type> value) {
]=], { name = enum.name, type = c_type(enum.name) })
  table_insert_template(c_helpers, [[
  switch (value) {
]])
  for _, option in ipairs(enum.values) do
    local enum_value = ("%s_%s"):format(cname, option.name):upper()
    table_insert_template(c_helpers, [[
  case <value>: return (struct u8_slice){.content = (const uint8_t *)"<option>", .len = <len>};
]], { value = enum_value, option = option.name, len = #option.name })
  end
  table_insert_template(c_helpers, [[
  default: assert(!"<name> value out of range");
]], { name = enum.name });
  table_insert_template(c_helpers, '  };\n}\n\n')
end

write_file(out_dir .. "/velvet_autogen_helpers.c", table.concat(c_helpers))

-- C Lua Functions {{{3

local c_marshal = {}
table_insert_template(c_marshal, [[
/***************************************************
************ DO NOT EDIT THIS BY HAND **************
*** This file was auto generated by api_gen.lua ****
***************************************************/

#include "lua.h"
#include "lauxlib.h"
#include "velvet_api.h"
#include "velvet_lua.h"
#include "velvet.h"
#include "velvet_autogen_helpers.c"

]])

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

  table_insert_template(c_marshal, [[

static int l_vv_api_<name>(lua_State *L) {
  lua_Integer argtop = <argc>;
  (void)argtop;
  struct velvet *v = *(struct velvet **)lua_getextraspace(L);
  if (v->reloading) {
    lua_pushstring(L, "vv.api cannot be used after calling reload.");
    lua_error(L);
  }
  v->current = L;
]], { name = fn.name, argc = #fn.params })

  local args = {}
  for idx, p in ipairs(fn.params or {}) do
    table.insert(args, p.name)
    local t = type_lookup[p.type]
    if is_manual(p.type) then
      -- pass the stack index; the implementation reads it from L
      table_insert_template(c_marshal, "  lua_Integer <name> = <idx>;\n", { name = p.name, idx = idx })
    elseif t.composite then
      if not t.optional then
        table_insert_template(c_marshal, [[
  luaL_checktype(L, <idx>, LUA_TTABLE);
]], { idx = idx })
      end
      table_insert_template(c_marshal, [[
  <type> <name> = {0};
  if (!lua_isnoneornil(L, <idx>)) {
    luaL_checktype(L, <idx>, LUA_TTABLE);
    lua_pushvalue(L, <idx>); /* push table to the top of the stack */
]], { type = c_type(p.type), name = p.name, idx = idx })

      check_field(c_marshal, p.type, p.name, 4)
      table_insert_template(c_marshal, [[
    lua_pop(L, 1); /* pop pushed table */
  }
]])
    elseif t and t.enumeration then
      local template = {
        slice = p.name .. '_str',
        name = p.name,
        check = lua_check('string', idx),
        struct = c_type(p.type),
        type = p.type,
      }
      if p.default_value == nil then
        table_insert_template(c_marshal, [[
  struct u8_slice <slice> = <check>;
  <struct> <name> = <type>_slice_to_enum(<slice>);
]], template)
      else
        template.idx = idx
        template.default_value = p.default_value
        table_insert_template(c_marshal, [[
  struct u8_slice <slice>;
  if (lua_isstring(L, <idx>)) {
    <slice> = <check>;
  } else {
    <slice> = u8_slice_from_cstr("<default_value>");
  }
  <struct> <name> = <type>_slice_to_enum(<slice>);
]], template)
      end
    else
      local template = { struct = c_type(p.type), name = p.name, check = lua_check(p.type, idx) }
      table_insert_template(c_marshal, "  <struct> <name> = <check>;\n", template)
    end
  end

  local argsstring = #args > 0 and ", " .. table.concat(args, ", ") or ""
  if has_manual_param or manual_return then
    table_insert_template(c_marshal, [[
  return vv_api_<name>(v<args>);
}
]], { name = fn.name, args = argsstring })
  elseif fn.returns.type == 'void' then
    table_insert_template(c_marshal, [[
  vv_api_<name>(v<args>);
  return 0;
}
]], { name = fn.name, args = argsstring })
  else
    table_insert_template(c_marshal, [[
  <struct> ret = vv_api_<name>(v<args>);
]], { struct = c_type(fn.returns.type), name = fn.name, args = argsstring })
    push_field(c_marshal, fn.returns.type, "ret")
    table_insert_template(c_marshal, '\n   return 1;\n}\n')
  end
end

-- Generate lua function table {{{3

table_insert_template(c_marshal, [=[
__attribute__((unused)) static const struct luaL_Reg velvet_lua_function_table[] = {
]=])

for _, fn in ipairs(spec.options) do
  table_insert_template(c_marshal, [[
  { "get_<name>", l_vv_api_get_<name> },
  { "set_<name>", l_vv_api_set_<name> },
]], { name = fn.name })
end

for _, fn in ipairs(spec.api) do
  local name = fn.name
  table_insert_template(c_marshal, [[
  { "<name>", l_vv_api_<name> },
]], { name = fn.name })
end


table_insert_template(c_marshal, '  {0} /* sentinel */\n};\n')
-- Write lua_autogen.c {{{3

write_file(out_dir .. "/velvet_lua_autogen.c", table.concat(c_marshal))


-- Event marshalling {{{3

local event_emitters = {}
table_insert_template(event_emitters, [[
/***************************************************
************ DO NOT EDIT THIS BY HAND **************
*** This file was auto generated by api_gen.lua ****
***************************************************/

#include "lua.h"
#include "velvet_api.h"
#include "velvet_lua.h"
#include "velvet.h"
#include "utils.h"
#include "velvet_autogen_helpers.c"
]])

for _, evt in ipairs(spec.events) do
  local event_name = evt.name:gsub("%.", "_")
  local event_arg_name = get_cname(evt.args)
  table_insert_template(event_emitters, [=[

void velvet_api_raise_<event>(struct velvet *v, __attribute__((unused)) struct <arg> args) {
  lua_State *L = v->L;
  if (!L) return;
  lua_getglobal(L, "vv");
  lua_getfield(L, -1, "events");
  lua_getfield(L, -1, "emit");
  lua_pushlstring(L, "<value>", <len>); /* event name */
]=], { event = event_name, arg = event_arg_name, value = evt.name, len = #evt.name })

  push_field(event_emitters, evt.args, "args")

  table_insert_template(event_emitters, [[
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
write_file(out_dir .. "/velvet_lua_event_emitters.c", table.concat(event_emitters))


-- LUA emitters {{{1

-- _api.lua {{{2
local api_meta = {}

table_insert_template(api_meta, [=[
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
  table_insert_template(api_meta, [[
---@alias <name> string <doc>
]], { name = lua_name, doc = enum.doc or "" })
  for _, v in ipairs(enum.values) do
    table_insert_template(api_meta, [[
---| '<name>' <doc>
]], { name = v.name, doc = v.doc or "" })
  end
  if enum.flags then
    table_insert_template(api_meta, [[

--- @class <name>s Flags for <name>
]], { name = lua_name })
    for _, value in ipairs(enum.values) do
      table_insert_template(api_meta, [[
--- @field <field>? boolean <doc>
]], { field = value.name, doc = value.doc or "" })
    end
  end
  table_insert_template(api_meta, '\n')
end

-- Generate type definitions for composite types {{{3

for _, type in ipairs(spec.types) do
  table_insert_template(api_meta, [[

--- @class <name>
]], { name = get_luaname(type.name) })
  for _, fld in ipairs(type.fields) do
    local lt = lua_type(fld.type) .. (fld.alias and ('|velvet.api.' .. fld.alias) or '')
    local t = type_lookup[fld.type]
    if t.enumeration and t.enumeration.flags then
      lt = ('%ss'):format(lt)
    end
    table_insert_template(api_meta, [[
--- @field <field> <type> <doc>
]], { field = fld.name .. (fld.optional and '?' or ''), type = lt, doc = fld.doc or "" })
  end
end

-- Generate api function spec {{{3

for _, fn in ipairs(spec.api) do
  table_insert_template(api_meta, [[

--- <doc>
]], { doc = string_concatenate(fn.doc, "\n--- ") })
  for _, p in ipairs(fn.params or {}) do
    local t = type_lookup[p.type]
    local optional = p.optional == true or t.optional == true or p.default_value ~= nil
    table_insert_template(api_meta, [[
--- @param <name> <type> <doc>
]], { name = p.name .. (optional and '?' or ''), doc = string_concatenate(p.doc, "\n--- "), type = lua_type(p.type) })
  end

  local params = {}
  for _, p in ipairs(fn.params or {}) do
    table.insert(params, p.name)
  end
  table_insert_template(api_meta, [[
--- @return <type> ret <doc>
]], { type = lua_type(fn.returns.type), doc = string_concatenate(fn.returns.doc, "\n--- ") })

  table_insert_template(api_meta, "function api.<name>(<params>) end\n",
    { name = fn.name, params = table.concat(params, ", ") })
end

-- Stub out event handlers {{{3

table_insert_template(api_meta, [[

--- @class velvet.api.event_handler
--- @field name string The name of the handler
--- @field id integer The id of the handler
]])
for _, evt in ipairs(spec.events) do
  table_insert_template(api_meta, [[
--- @field <field>? fun(event_args: velvet.api.<args>): nil <doc>
]], { field = evt.name:gsub('%.', '_'), args = evt.args, doc = evt.doc })
end

-- Write _api.lua {{{3

table_insert_template(api_meta, "\n")
write_file("lua/velvet/_api.lua", table.concat(api_meta))

-- _options.lua {{{2

local options = {}
table_insert_template(options, [[
error("Cannot require meta file")
--- @meta
--- @class velvet.options
local options = {}
]])

for _, fn in ipairs(spec.options) do
  local luatype = lua_type(fn.type)
  table_insert_template(options, [[
--- <doc>
--- @type <type>
options.<name> = <default_value>

]], { doc = string_concatenate(fn.doc, "\n--- "), type = luatype, name = fn.name, default_value = inspect(fn.default) })
end

table_insert_template(options, "return options\n")
write_file("lua/velvet/_options.lua", table.concat(options))

-- generate options.lua {{{2

local default_options = {}

table_insert_template(default_options, [[
--- DO NOT EDIT THIS BY HAND
--- This file was auto generated by api_gen.lua
--- It sets all options to their default values.

]])

--- Set default options {{{3

for _, fn in ipairs(spec.options) do
  table_insert_template(default_options, [[
vv.options.<name> = <default_value>
]], { name = fn.name, default_value = inspect(fn.default) })
end


--- write file {{{3

table_insert_template(default_options, "\n")
write_file("lua/velvet/default_options.lua", table.concat(default_options))

--- generate async.lua {{{2
local async = {}
table_insert_template(async, [==[
--[[
DO NOT EDIT THIS BY HAND
This file was auto generated by api_gen.lua
--]]

local M = {}

local function when_impl(reg, event)
  return reg.inner_when(event.data)
end

local function wait_impl(event, timeout, when)
  local registration = event
  if when then
    registration = { event = event, inner_when = when, when = when_impl }
  end
  local _, result = M.wait(registration, timeout)
  return result.data
end

--- @type table<string, string|boolean>
local known_events = {
]==])

--- Description of known events {{{3
for _, evt in ipairs(spec.events) do
  table_insert_template(async, "  [ [[<name>]] ] = [[<doc>]],\n", { name = evt.name, doc = evt.doc })
end

--- Event name type alias {{{3
table_insert_template(async, [[
}

--- @alias velvet.async.event
]])
for _, evt in ipairs(spec.events) do
  table_insert_template(async, "---| '<name>' <doc>\n", { name = evt.name, doc = evt.doc })
end

--- Generate user-facing API {{{3
for _, evt in ipairs(spec.events) do
  table_insert_template(async, [[

--- Wait for <name>
--- @param timeout? integer Optional timeout.
--- @param when? velvet.async.single_when<velvet.api.<return_type>> predicate function
--- @return velvet.api.<return_type> ret Result, or nil on timeout.
function M.wait_for_<event>(timeout, when)
  return wait_impl('<name>', timeout, when)
end
]], { name = evt.name, return_type = evt.args, event = evt.name:gsub('%.', '_') })
end

table_insert_template(async, "\nreturn { known_events, M }")

--- write file {{{3
table_insert_template(async, "\n")
write_file("lua/velvet/async/autogen.lua", table.concat(async))

-- Modeline {{{1
-- vim: fdm=marker shiftwidth=2
