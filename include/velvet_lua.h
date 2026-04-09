#ifndef VELVET_LUA_H
#define VELVET_LUA_H
#include "velvet.h"

void velvet_lua_init(struct velvet *v);
void velvet_lua_source(struct velvet *v, char *path);
void velvet_source_config(struct velvet *v);
int lua_debug_traceback_handler(lua_State *L);

#endif /* VELVET_LUA_H */
