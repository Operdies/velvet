#include "velvet.h"
#include "lauxlib.h"
#include "utils.h"
#include <errno.h>


static int l_socket_print(lua_State *L) {
  static struct string linebuf = {0};
  string_clear(&linebuf);
  int source_socket = lua_tointeger(L, lua_upvalueindex(1));
  if (source_socket == 0) return 0;
  int n = lua_gettop(L);
  for (int i = 1; i <= n; i++) {
    struct u8_slice s;
    s.content = (uint8_t*)luaL_tolstring(L, i, &s.len);
    if (i > 1) string_push_char(&linebuf, '\t');
    string_push_slice(&linebuf, s);
    lua_pop(L, 1);
  }
  string_push_char(&linebuf, '\n');
  /* the socket is nonblocking, so if the write is too large it won't go through.
   * the alternative is to lock the while server, so let's just drop some messages instead.
   *
   * The golden solution would be to make velvet_dispatch() re-entrant. Maybe later.
   * But really, if someone wants to extract huge chunks of text, maybe they should use
   * io instead of print()
   * */
  write(source_socket, linebuf.content, linebuf.len);
  return 0;
}

void velvet_lua_execute_chunk(struct velvet *v, struct u8_slice chunk, int source_socket) {
  lua_State *L = v->L;
  set_nonblocking(source_socket);

  // Build a custom env: setmetatable({print = socket_print}, {__index = _G})
  lua_pushinteger(L, source_socket);
  lua_pushcclosure(L, l_socket_print, 1); // socket_print (socket as upvalue)
  lua_newtable(L);                        // env = {}
  lua_pushvalue(L, -2);
  lua_setfield(L, -2, "print"); // env.print = socket_print
  lua_newtable(L);              // mt = {}
  lua_getglobal(L, "_G");
  lua_setfield(L, -2, "__index"); // mt.__index = _G
  lua_setmetatable(L, -2);        // setmetatable(env, mt)
  // stack: [socket_print, env]

  int socket_print_idx = lua_gettop(L) - 1;
  int env_idx = lua_gettop(L);

  if (luaL_loadbuffer(L, (char *)chunk.content, chunk.len, "=(lua cmd)") != LUA_OK) {
    size_t len = 0;
    const char *err = lua_tolstring(L, -1, &len);
    if (source_socket)
      write(source_socket, err, len);
    else
      velvet_log("lua cmd error: %s", err);
  } else {
    lua_pushvalue(L, env_idx);
    lua_setupvalue(L, -2, 1); // chunk._ENV = env
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
      size_t len = 0;
      const char *err = lua_tolstring(L, -1, &len);
      if (source_socket)
        write(source_socket, err, len);
      else
        velvet_log("lua cmd error: %s", err);
    }
  }

  // Invalidate socket_print by zeroing the upvalue
  lua_pushinteger(L, 0);
  lua_setupvalue(L, socket_print_idx, 1);

  lua_pop(L, lua_gettop(L));
}

static int read_digit(struct u8_slice s, int *i32) {
  if (s.len == 0) return false;
  size_t i, v;
  i = v = 0;
  for (; i < s.len; i++) {
    uint8_t ch = s.content[i];
    if (!(ch >= '0' && ch <= '9')) break;
    v *= 10;
    v += ch - '0';
  }
  *i32 = v;
  return i;
}

void velvet_cmd(struct velvet *v, int source_socket, struct u8_slice cmd) {
  int codelength;
  int read = read_digit(cmd, &codelength);
  if (!read) {
    io_write(source_socket, u8_slice_from_cstr("Expected command to start with length encoding"));
    return;
  }

  v->socket_cmd_sender = source_socket;
  const uint8_t *chunk_start = cmd.content + read;

  size_t remaining = (cmd.content + cmd.len) - chunk_start;
  if (remaining != (size_t)codelength) {
    io_write_format_slow(source_socket, "lua chunk length does not match encoded length. "
                         "This could be due to a partial write (chunk too large),"
                         "or it could be a bug!");
  } else {
    struct u8_slice chunk = {.len = codelength, .content = chunk_start};
    velvet_lua_execute_chunk(v, chunk, source_socket);
  }
}
