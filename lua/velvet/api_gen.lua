-- Setup {{{1
local spec_path = assert(arg[1], "usage: lua api_gen.lua <api_spec.lua> <out_dir>")
local out_dir = assert(arg[2], "usage: lua api_gen.lua <api_spec.lua> <out_dir>")
local spec = dofile(spec_path)

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

for i, fn in ipairs(spec.options) do
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
    returns = { type = fn.type, doc = "The value after the updated" }
  }
  table.insert(spec.api, getter)
  table.insert(spec.api, setter)
end

for i, fn in ipairs(spec.api) do 
  fn.parameters = fn.parameters or {}
  fn.optional = fn.optional or {}
  fn.returns = fn.returns or { type = "void" }
end

-- Type Utilities {{{1

local type_lookup = {
  void = { c_type = "void", lua_type = "nil" },
  int = {
    c_type = "int",
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
      return ("luaL_checklstring(L, %d, nullptr)"):format(idx)
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
}

local function lua_type(t)
  if t == nil then return "nil" end
  return type_lookup[t].lua_type
end


local function c_type(t)
  local entry = type_lookup[t] or error("No such type: " .. t)
  return entry.c_type
end

local function lua_check(t, idx)
  return type_lookup[t].check(idx)
end

local function lua_push(t, var)
  return type_lookup[t].push(var)
end

-- C Emitters {{{1

-- C Header {{{2
local h = {}
table.insert(h, ("#ifndef %s\n#define %s\n"):format("VELVET_API_H", "VELVET_API_H"))
table.insert(h, "struct velvet;\n\n")

for i, fn in ipairs(spec.api) do
  local required = {}
  for i, p in ipairs(fn.params or {}) do
    table.insert(required, c_type(p.type) .. " " .. p.name)
  end

  local optional = {}
  for i, o in ipairs(fn.optional or {}) do
    table.insert(optional, c_type(o.type) .. "* " .. o.name)
  end

  local required_params = #required > 0 and ", " .. table.concat(required, ", ") or ""
  local optional_params = #optional > 0 and ", " .. table.concat(optional, ", ") or ""

  table.insert(h, ("/* %s */\n"):format(fn.doc))
  table.insert(h,
    ("%s vv_api_%s(struct velvet *v%s%s);\n")
    :format(c_type(fn.returns.type), fn.name, required_params, optional_params)
  )
end
table.insert(h, ("#endif /* %s */\n"):format("VELVET_API_H"))

write_file(out_dir .. "/velvet_api.h", table.concat(h))

-- C Lua Functions {{{2

local c = {}
table.insert(c, [[
#include "lua.h"
#include "lauxlib.h"
#include "velvet_api.h"

static bool luaL_checkboolean(lua_State *L, int idx) {
  luaL_checktype(L, idx, LUA_TBOOLEAN);
  return lua_toboolean(L, idx);
}
]])

for i, fn in ipairs(spec.api) do
  local params = {}
  for i, p in ipairs(fn.params or {}) do
    table.insert(params, c_type(p.type) .. " " .. p.name)
  end

  table.insert(c,
    ([[

static int l_vv_api_%s(lua_State *L){
  struct velvet *v = *(struct velvet**)lua_getextraspace(L);
]])
    :format(fn.name)
  )

  local idx = 1
  local args = {}
  for i, p in ipairs(fn.params or {}) do
    table.insert(c,
      ("  %s %s = %s;\n")
      :format(c_type(p.type), p.name, lua_check(p.type, idx))
    )
    table.insert(args, p.name)
    idx = idx + 1
  end

  if #fn.optional > 0 then
    for i, o in ipairs(fn.optional) do
      table.insert(c, ([[
  %s %s;
  %s* p_%s = nullptr;
]]):format(c_type(o.type), o.name, c_type(o.type), o.name))
      table.insert(args, "p_" .. o.name)
    end

    table.insert(c, ([[
  if (!lua_isnoneornil(L, %d)) {
    luaL_checktype(L, %d, LUA_TTABLE);
]]):format(idx, idx, idx))

    for i, o in ipairs(fn.optional) do
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
  else
    table.insert(c, ([[
  %s ret = vv_api_%s(v%s);
  %s;
  return 1;
}
]]):format(c_type(fn.returns.type), fn.name, argsstring, lua_push(fn.returns.type, "ret")))
  end
end

table.insert(c, [=[
[[maybe_unused]] static const struct luaL_Reg velvet_lua_function_table[] = {
]=])

for i, fn in ipairs(spec.options) do
  local name = fn.name
  table.insert(c, ([[
  { "get_%s", l_vv_api_get_%s },
  { "set_%s", l_vv_api_set_%s },
]]):format(name, name, name, name))
end

for i, fn in ipairs(spec.api) do
  local name = fn.name
  table.insert(c, ([[
  { "%s", l_vv_api_%s },
]]):format(name, name))
end


table.insert(c, [[
  {0} /* sentinel */
};
]])

write_file(out_dir .. "/velvet_lua_autogen.c", table.concat(c))

-- LUA emitters {{{1

-- _api.lua {{{2
local lua = {}

table.insert(lua, [[
error("Cannot require meta file")
--- @meta
--- @class velvet.api
local api = {}
]])

for i, fn in ipairs(spec.api) do
  local optional = fn.optional or {}
  if #optional > 0 then
    table.insert(lua, ([[

--- @class velvet.api.%s.Opts
]]):format(fn.name))
    for i, opt in ipairs(optional) do
      table.insert(lua, ([[
--- @field %s? %s %s
]]):format(opt.name, lua_type(opt.type), opt.doc))
    end
  end
  table.insert(lua, ([[

--- %s
]]):format(fn.doc))
  for i, p in ipairs(fn.params or {}) do
    table.insert(lua, ([[
--- @param %s %s %s
--- 
]]):format(p.name, lua_type(p.type), p.doc))
  end

  local params = {}
  for i, p in ipairs(fn.params or {}) do
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
]]):format(lua_type(fn.returns.type), fn.returns.doc))

  table.insert(lua,
    ("function api.%s(%s) end\n")
    :format(fn.name, table.concat(params, ", "))
  )
end

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

for i, fn in ipairs(spec.options) do
  local luatype = lua_type(fn.type)
  table.insert(options, ([[
--- %s
--- @type %s
options.%s = %s

]]):format(fn.doc, luatype, fn.name, fn.default))
end

table.insert(options, "return options\n")
write_file("lua/velvet/_options.lua", table.concat(options))

-- generate options.lua {{{2

local default_options = {}

table.insert(default_options, [[
--- This file was auto generated by api_gen.lua
--- It sets all options to their default values.
]])

for i, fn in ipairs(spec.options) do
  table.insert(default_options, ([[
vv.options.%s = %s
]]):format(fn.name, fn.default))
end

table.insert(default_options, "\n")
write_file("lua/velvet/default_options.lua", table.concat(default_options))


-- Modeline {{{1
-- vim: fdm=marker shiftwidth=2
