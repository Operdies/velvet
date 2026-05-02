#ifndef VELVET_LUA_H
#define VELVET_LUA_H
#include "velvet.h"

void velvet_lua_init(struct velvet *v);
void velvet_lua_source(struct velvet *v, char *path);
void velvet_source_config(struct velvet *v);
int lua_debug_traceback_handler(lua_State *L);
struct u8_slice luaL_checkslice(lua_State *L, lua_stackIndex idx);
void lua_pushslice(lua_State *L, struct u8_slice s);
lua_Integer luaL_checkfunction(lua_State *L, lua_stackIndex idx);
lua_Integer luaL_checktable(lua_State *L, lua_stackIndex idx);
bool luaL_checkboolean(lua_State *L, lua_stackIndex idx);

#endif /* VELVET_LUA_H */
