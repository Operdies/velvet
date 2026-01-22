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

-- Type Utilities {{{1

local type_lookup = {
  ["int[]"] = { lua_type = "integer[]" },
  void = { c_type = "void", lua_type = "nil" },
  ["function"] = {
    c_type = "lua_Integer",
    lua_type = "fun(): nil",
    check = function(idx)
      return ("luaL_checkfunction(L, %d)"):format(idx)
    end,
  },
  int = {
    c_type = "lua_Integer",
    lua_type = "integer",
    check = function(idx)
      return ("luaL_checkinteger(L, %d)"):format(idx)
    end,
    push = function(var) return ("lua_pushinteger(L, %s)"):format(var) end
  },
  string = {
    c_type = "const char*",
    lua_type = "string",
    check = function(idx)
      return ("luaL_checklstring(L, %d, NULL)"):format(idx)
    end,
    push = function(var) return ("lua_pushstring(L, %s)"):format(var) end
  },
  bool = {
    c_type = "bool",
    lua_type = "boolean",
    check = function(idx)
      return ("luaL_checkboolean(L, %d)"):format(idx)
    end,
    push = function(var) return ("lua_pushboolean(L, %s)"):format(var) end
  },
  float = {
    c_type = "float",
    lua_type = "number",
    check = function(idx)
      return ("luaL_checknumber(L, %d)"):format(idx)
    end,
    push = function(var) return ("lua_pushnumber(L, %s)"):format(var) end
  },
}

local function get_cname(name)
  return "velvet_api_" .. name:gsub("[.]", "_")
end

local function get_luaname(name)
  return "velvet.api." .. name
end

local function is_complex(name) 
  if type_lookup[name] then return type_lookup[name].complex  end
  error(("is_complex: unknown type: %s"):format(name))
end

local function is_manual(name) 
  -- types we know that we cannot automatically marshal. Such functions must be implemented by hand.
  local manual_types = { ["int[]"] = true }
  return manual_types[name]
end


--- @type table<string,spec_type>
local complex_index = {}
for _, type in ipairs(spec.types) do
  type_lookup[type.name] = { c_type = "struct " .. get_cname(type.name), lua_type = get_luaname(type.name), complex = true }
  complex_index[type.name] = type
end

for _, type in ipairs(spec.enums) do
  type_lookup[type.name] = { c_type = "enum " .. get_cname(type.name), lua_type = get_luaname(type.name), enum = true, push = type_lookup.int.push, check = type_lookup.int.check }
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
  if is_complex(type) then
    local complex = complex_index[type]
    table.insert(tbl, ([[
  lua_newtable(L); /* %s */
]]):format(path, complex.name))
    for _, mem in ipairs(complex.fields) do
      local mem_path = path .. "." .. mem.name
      push_field(tbl, mem.type, mem_path)
      table.insert(tbl, ([[
  lua_setfield(L, -2, "%s"); /* %s = %s */
]]):format(mem.name, mem_path, mem.name))
    end
  else
    table.insert(tbl, ([[
  %s;
]]):format(lua_push(type, path)))
  end
end

--- @param tbl table
--- @param type string
--- @param path string
local function check_field(tbl, type, path)
  if is_complex(type) then
    local complex = complex_index[type]
    table.insert(tbl, [[
  luaL_checktype(L, -1, LUA_TTABLE);
]])
    for _, mem in ipairs(complex.fields) do
      local mem_path = path .. "." .. mem.name
      table.insert(tbl, ([[
  lua_getfield(L, -1, "%s"); /* get %s */
]]):format(mem.name, mem_path))
      check_field(tbl, mem.type, mem_path)
      table.insert(tbl, [[
  lua_pop(L, 1);
]])
    end
  else
    table.insert(tbl, ([[
  %s = %s;
]]):format(path, lua_check(type, -1)))
  end
end

-- C Emitters {{{1

-- C Header {{{2
local h = {}
table.insert(h, ([[#ifndef %s
/***************************************************
************ DO NOT EDIT THIS BY HAND **************
*** This file was auto generated by api_gen.lua ****
***************************************************/

#define %s
#include "lua.h"
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
-- Create structs for complex types.
-- These structs will automatically be marshaled to and from lua
for _, type in ipairs(spec.types) do
  local cname = get_cname(type.name)
  table.insert(h, ([[
struct %s {
]]):format(cname))
  for _, fld in ipairs(type.fields) do
    table.insert(h, ([[
  %s %s; /* %s */
]]):format(c_type(fld.type), fld.name, string_concatenate(fld.doc, "")))
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

-- C Lua Functions {{{2

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

-- C API function marshalling {{{2

for _, fn in ipairs(spec.api) do
  local params = {}
  for _, p in ipairs(fn.params or {}) do
    table.insert(params, c_type(p.type) .. " " .. p.name)
  end

  table.insert(c, ([[

static int l_vv_api_%s(lua_State *L){
]])
    :format(fn.name))
  if not is_manual(fn.returns.type) then
    table.insert(c, '  struct velvet *v = *(struct velvet**)lua_getextraspace(L);\n')
  end

  local idx = 1
  local args = {}
  for _, p in ipairs(fn.params or {}) do
    if is_complex(p.type) then
      table.insert(c, ([[
  luaL_checktype(L, %d, LUA_TTABLE);
  %s %s = {0};
  lua_pushvalue(L, %d); /* push table to the top of the stack */
]]):format(idx, c_type(p.type), p.name, idx))
      check_field(c, p.type, p.name)
      table.insert(c, [[
  lua_pop(L, 1); /* pop pushed table */
]])
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
  elseif is_complex(fn.returns.type) then
    table.insert(c, ([[
  %s ret = vv_api_%s(v%s);
]]):format(c_type(fn.returns.type), fn.name, argsstring))
    push_field(c, fn.returns.type, "ret")
    table.insert(c, [[
  return 1;
}
]]);
  else
    table.insert(c, ([[
  %s ret = vv_api_%s(v%s);
  %s;
  return 1;
}
]]):format(c_type(fn.returns.type), fn.name, argsstring, lua_push(fn.returns.type, "ret")))
  end
end

-- Generate lua function table {{{2

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

--- Event marshalling {{{2

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

--- Write lua_autogen.c {{{2

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

local function need_quote(name)
  local keywords = { ["return"] = true, ["repeat"] = true, ["break"] = true, ["end"] = true }
  return name:match('[\'",. ]') or keywords[name] or false
end

for _, enum in ipairs(spec.enums) do
  local lua_name = get_luaname(enum.name)
  table.insert(lua, ([[

--- @enum %s
api.%s = {
]]):format(lua_name, enum.name))
  for _, v in ipairs(enum.values) do
    if need_quote(v.name) then
      table.insert(lua, ([[
  ['%s'] = %d,
]]):format(v.name, v.value))
    else
      table.insert(lua, ([[
  %s = %d,
]]):format(v.name, v.value))
    end
  end
  table.insert(lua, [[
}
]])
end

-- Generate type definitions for complex types {{{3

for _, type in ipairs(spec.types) do
  local lua_name = get_luaname(type.name)
  table.insert(lua, ([[

--- @class %s
]]):format(lua_name))
  for _, fld in ipairs(type.fields) do
    table.insert(lua, ([[
--- @field %s %s %s
]]):format(fld.name, lua_type(fld.type), fld.doc))
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
    table.insert(lua, ([[
--- @param %s %s %s
]]):format(p.name, lua_type(p.type), string_concatenate(p.doc, "\n--- ")))
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
--- @return %s %s
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

--- Populate enum values {{{3

table.insert(default_options, [[

--- Populate all enum tables. These enums are used for interfacing with API functions
]])

for _, enum in ipairs(spec.enums) do
  table.insert(default_options, ([[

vv.api.%s = {
]]):format(enum.name))
  for _, v in ipairs(enum.values) do
    if need_quote(v.name) then
      table.insert(default_options, ([[
  ['%s'] = %d,
]]):format(v.name, v.value))
    else
      table.insert(default_options, ([[
  %s = %d,
]]):format(v.name, v.value))
    end
  end
  table.insert(default_options, [[
}
]])
end

--- write file {{{3

table.insert(default_options, "\n")
write_file("lua/velvet/default_options.lua", table.concat(default_options))


-- Modeline {{{1
-- vim: fdm=marker shiftwidth=2
