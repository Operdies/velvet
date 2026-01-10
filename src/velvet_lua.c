#include "velvet_lua.h"
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
#include "velvet_lua_autogen.c"

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

static void pcall_func_ref(lua_State *L, int func_ref) {
  lua_rawgeti(L, LUA_REGISTRYINDEX, func_ref);

  if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
    const char *err = lua_tostring(L, -1);
    velvet_log("lua error: %s", err);
    lua_pop(L, 1);
  }
}

static struct u8_slice luaL_checkslice(lua_State *L, int pos) {
  size_t name_len;
  const char *name = luaL_checklstring(L, pos, &name_len);
  struct u8_slice s = {.content = (uint8_t *)name, .len = name_len};
  return s;
}

static void keymap_execute(struct velvet_keymap *k, struct velvet_key_event evt) {
  struct velvet *v = k->root->data;
  lua_State *L = v->L;
  int lua_function = (int)(int64_t)k->data;
  if (evt.removed) {
    luaL_unref(L, LUA_REGISTRYINDEX, lua_function);
  } else {
    pcall_func_ref(L, lua_function);
  }
}

static int keymap_add(lua_State *L) {
  struct velvet *vv = *(struct velvet **)lua_getextraspace(L);
  struct u8_slice keys = luaL_checkslice(L, 1);
  luaL_checktype(L, 2, LUA_TFUNCTION);

  struct velvet_keymap *added = velvet_keymap_map(vv->input.keymap->root, keys);
  if (added) {
    added->data = vv;
    lua_pushvalue(L, 2); /* copy function to top of stack */
    int cb_ref =
        luaL_ref(L, LUA_REGISTRYINDEX); /* create a gc handle. This handle is cleared when the keymap is removed */
    added->data = (void *)(int64_t)cb_ref;
    added->on_key = keymap_execute;
  }
  return 0;
}

static int keymap_del(lua_State *L) {
  struct velvet *vv = *(struct velvet **)lua_getextraspace(L);
  struct u8_slice keys = luaL_checkslice(L, 1);
  velvet_keymap_unmap(vv->input.keymap->root, keys);
  return 0;
}

static void velvet_lua_init_keymap(struct velvet *v) {
  lua_State *L = v->L;
  struct luaL_Reg keymap_functions[] = {
      {"set", keymap_add},
      {"del", keymap_del},
      {0},
  };

  lua_getglobal(L, "vv");
  luaL_newlib(L, keymap_functions);
  lua_setfield(L, 1, "keymap");
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
  luaL_dostring(L, "require('velvet.default_options')");
  lua_pop(L, lua_gettop(L));
}

void velvet_lua_init(struct velvet *v) {
  lua_State *L = lua_newstate(lua_allocator, nullptr, 0);
  v->L = L;
  luaL_openselectedlibs(v->L, ~LUA_DBLIBK, 0);
  struct velvet **extra = lua_getextraspace(v->L);
  *extra = v;

  /* disable loading modules from the system path and C libraries. */
  /* [1]: preload table, [2]: package.path */
  if (luaL_dostring(L, "package.searchers = { package.searchers[1], package.searchers[2] }")) {
    lua_die(L);
  }
  if (luaL_dostring(L, "package.path = './lua/?/init.lua;./lua/?.lua'") != LUA_OK) {
    lua_die(L);
  }

  luaL_dostring(L, "vv = require('velvet')");
  velvet_lua_init_api(v);
  velvet_lua_init_keymap(v);
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
