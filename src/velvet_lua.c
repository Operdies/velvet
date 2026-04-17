#include "velvet_lua.h"
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
#include "velvet_lua_autogen.c"
#include <string.h>
#include <sys/stat.h>
#include "platform.h"

static void *lua_allocator(void *ud, void *ptr, size_t osize, size_t nsize) {
  (void)osize;
  (void)ud;
  if (nsize == 0) {
    free(ptr);
    ptr = NULL;
  } else {
    ptr = realloc(ptr, nsize);
  }
  return ptr;
}

int lua_debug_traceback_handler(lua_State *L) {
  luaL_traceback(L, L, lua_tostring(L, 1), 1);
  return 1;
}

// static int l_velvet_log(lua_State *L) {
//   const char *str = luaL_checkstring(L, 1);
//   velvet_log("lua: %s", str);
//   return 0;
// }

static void velvet_lua_init_coroutine_helper(struct velvet *v) {
  char coroutine_helper[] = {
      "return function(chunk, setup, cleanup, print_function, arg0, ...)\n"
      "  local cli_args = {...}\n"
      "  cli_args[0] = arg0\n"
      "  local function user_traceback(msg)\n"
      "    local tb = debug.traceback(msg, 2)\n"
      "    local lines = {}\n"
      "    for line in tb:gmatch('[^\\n]+') do\n"
      "      lines[#lines + 1] = line\n"
      "      if line:find('%(lua cmd%)') then break end\n"
      "    end\n"
      "    return table.concat(lines, '\\n')\n"
      "  end\n"
      "  vv.async.run(function()\n"
      "    setup()\n"
      "    COROUTINE_PRINT[coroutine.running()] = print_function\n"
      "    COROUTINE_ARGS[coroutine.running()] = cli_args\n"
      "    local ok, result = xpcall(chunk, user_traceback)\n"
      "    if result then result = type(result) == 'string' and result or require('velvet.json').to_json(result) end\n"
      "    if not ok then\n"
      "      print_function(2, 'Unhandled error in lua chunk: ' .. result)\n"
      "    elseif result ~= nil then\n"
      "      print_function(1, result)\n"
      "    end\n"
      "    cleanup(ok and true or false)\n"
      "  end)\n"
      "end",
  };
  int status = luaL_loadbuffer(v->L, coroutine_helper, strlen(coroutine_helper), "@velvet.cli");
  lua_call(v->L, 0, 1);
  assert(status == LUA_OK);
  assert(lua_type(v->L, -1) == LUA_TFUNCTION);
  v->coroutine_wrapper_function = luaL_ref(v->L, LUA_REGISTRYINDEX);
}

static void velvet_lua_init_arg(struct velvet *v) {
  lua_State *L = v->L;
  lua_getglobal(L, "vv");
  lua_newtable(L);
  int i = 1;
  /* push velvet binary to arg[0]. The lua interpreter uses arg[0]
   * to set the script path with e.g. `lua myscript.lua`,
   * so this should be similar. */
  char *exepath = platform_get_exe_path();
  lua_pushstring(L, exepath);
  lua_rawseti(L, -2, 0);
  char **arg = v->positional_args;
  for (;arg && *arg; arg++, i++) {
    lua_pushstring(L, *arg);
    lua_seti(L, -2, i);
  }
  lua_setfield(L, -2, "startup_arguments");
}

static void velvet_lua_init_api(struct velvet *v) {
  lua_State *L = v->L;
  lua_getglobal(L, "vv");
  luaL_newlib(L, velvet_lua_function_table);
  lua_setfield(L, 1, "api");
  lua_pop(L, lua_gettop(L));
}

static void velvet_lua_set_default_options(struct velvet *v) {
  lua_State *L = v->L;
  if (luaL_dostring(L, "require('velvet.default_options')") != LUA_OK)
    lua_die(L);
  lua_pop(L, lua_gettop(L));
}

static bool file_exists(const char *path) {
  struct stat st;
  return stat(path, &st) == 0;
}

void velvet_source_config(struct velvet *v) {
  struct string scratch = {0};
  char *home = getenv("HOME");
  if (home) {
    string_push_format_slow(&scratch, "%s/.config/velvet/init.lua", home);
    /* preserve 0 terminator */
    scratch.len++;
  }
  if (file_exists((char*)scratch.content)) {
    size_t offset = scratch.len;
    /* lua code to add the user's config folder to the module search path */
    string_push_format_slow(&scratch, "package.path = package.path .. ';%s/.config/velvet/?/init.lua;%s/.config/velvet/?.lua'", home, home);
    struct u8_slice search_path = string_range(&scratch, offset, -1);
    if (luaL_dostring(v->L, (char*)search_path.content) != LUA_OK) lua_die(v->L);
    velvet_lua_source(v, (char*)scratch.content);
  } else {
    /* if the user does not have a config file, source the default config */
    if (luaL_dostring(v->L, "require('velvet.default_config')") != LUA_OK) lua_die(v->L);
  }
  string_destroy(&scratch);
}

void velvet_lua_init(struct velvet *v) {
  assert(!v->L);
  lua_State *L = lua_newstate(lua_allocator, NULL, 0);
  v->L = L;
  luaL_openselectedlibs(v->L, ~0, 0);
  struct velvet **extra = lua_getextraspace(v->L);
  *extra = v;

  /* disable loading modules from the system path and C libraries. */
  /* [1]: preload table, [2]: package.path */
  if (luaL_dostring(L, "package.searchers = { package.searchers[1], package.searchers[2] }")) {
    lua_die(L);
  }

  char *binpath = platform_get_exe_path();
  char *lastslash = strrchr(binpath, '/');
  *lastslash = 0;
  if (chdir(binpath) == -1)
    velvet_die("chdir:");
  free(binpath);

  /* The lua distribution is in the parent directory of the folder containing the vv binaries.
   * We don't want to load lua libraries from any other directories by default.
   * End users can manually add search paths if they want to. */
  if (luaL_dostring(L, "package.path = '../lua/?/init.lua;../lua/?.lua'") != LUA_OK) {
    lua_die(L);
  }

  /* set `vv` in the global scope */
  if (luaL_dostring(L, "vv = require('velvet')") != LUA_OK) {
    lua_die(L);
  }

  velvet_lua_init_api(v);
  /* set args from command line */
  velvet_lua_init_arg(v);
  velvet_lua_init_coroutine_helper(v);
  // velvet_lua_init_log(v);
  velvet_lua_set_default_options(v);
}

static void vv_log_lua_error(struct velvet *vv) {
  struct u8_slice err;
  err.content = (const uint8_t*)luaL_tolstring(vv->L, 1, &err.len);
  velvet_log("lua error: %.*s", (int)err.len, err.content);
  struct velvet_api_system_message_event_args event_args = { .message = err, .level = VELVET_API_SEVERITY_ERROR };
  velvet_api_raise_system_message(vv, event_args);
}

void velvet_lua_source(struct velvet *vv, char *path) {
  lua_getglobal(vv->L, "dofile");
  lua_pushstring(vv->L, path);
  if (lua_pcall(vv->L, 1, 0, 0) != LUA_OK) {
    vv_log_lua_error(vv);
  }
}

struct u8_slice luaL_checkslice(lua_State *L, lua_stackIndex idx) {
  struct u8_slice s;
  s.content = (const uint8_t*)luaL_checklstring(L, idx, &s.len);
  return s;
}

lua_Integer luaL_checkfunction(lua_State *L, lua_stackIndex idx) {
  luaL_checktype(L, idx, LUA_TFUNCTION);
  return idx;
}

bool luaL_checkboolean(lua_State *L, lua_stackIndex idx) {
  luaL_checktype(L, idx, LUA_TBOOLEAN);
  return lua_toboolean(L, idx);
}

void lua_pushslice(lua_State *L, struct u8_slice s) {
  if (s.content) lua_pushlstring(L, (const char*)s.content, s.len);
  else lua_pushstring(L, NULL);
}
