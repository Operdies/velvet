#include "velvet_lua.h"
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
#include "velvet_lua_autogen.c"
#include <string.h>
#include "platform.h"

static void *lua_allocator(void *ud, void *ptr, size_t osize, size_t nsize) {
  (void)osize;
  (void)ud;
  if (nsize == 0) {
    free(ptr);
    ptr = nullptr;
  } else {
    ptr = realloc(ptr, nsize);
  }
  return ptr;
}

static int l_velvet_log(lua_State *L) {
  const char *str = luaL_checkstring(L, 1);
  velvet_log("lua: %s", str);
  return 0;
}

static void velvet_lua_init_log(struct velvet *v) {
  lua_State *L = v->L;
  lua_getglobal(L, "vv");
  lua_pushcfunction(L, l_velvet_log);
  lua_setfield(L, -2, "print");
  lua_pushglobaltable(L);
  lua_pushcfunction(L, l_velvet_log);
  lua_setfield(L, -2, "print");
  lua_pop(L, lua_gettop(L));
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

void velvet_lua_init(struct velvet *v) {
  lua_State *L = lua_newstate(lua_allocator, nullptr, 0);
  v->L = L;
  luaL_openselectedlibs(v->L, ~(LUA_IOLIBK), 0);
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
  velvet_lua_init_log(v);
  velvet_lua_set_default_options(v);
}

static void vv_log_lua_error(struct velvet *vv) {
  const char *str = luaL_tolstring(vv->L, 1, 0);
  velvet_log("lua error: %s", str);
}

void velvet_lua_source(struct velvet *vv, char *path) {
  lua_getglobal(vv->L, "dofile");
  lua_pushstring(vv->L, path);
  if (lua_pcall(vv->L, 1, 0, 0) != LUA_OK) {
    vv_log_lua_error(vv);
  }
}
