#include "velvet_cmd.h"
#include "lauxlib.h"
#include "utils.h"

static bool is_whitespace(char ch) {
  return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v';
}

bool velvet_cmd_iterator_next(struct velvet_cmd_iterator *it) {
  struct u8_slice t = it->src;
  size_t c0 = it->cursor;
  size_t cursor = it->cursor;

  for (; cursor < t.len;) {
    char ch = t.content[cursor];
    if (ch == '#') {
      while (cursor < t.len && t.content[cursor] != '\n') cursor++;
    } else if (is_whitespace(ch) || ch == ';') {
      for (; cursor < t.len && (is_whitespace(t.content[cursor]) || t.content[cursor] == ';'); cursor++);
    } else {
      break;
    }
  }

  size_t start = cursor;
  if (start >= t.len) {
    it->cursor = cursor;
    return false;
  }

  bool escape = false;
  bool terminator_found = false;
  char quote = 0;
  for (cursor = start; cursor < t.len; cursor++) {
    char ch = t.content[cursor];
    if (!escape && ch == '\\') {
      escape = true;
    } else if (escape) {
      escape = false;
    } else if (ch == '\n' || ch == 0) {
      terminator_found = true;
      break;
    } else if (quote && ch != quote) {
      continue;
    } else if (quote && ch == quote) {
      quote = 0;
    } else if (ch == '"' || ch == '\'') {
      quote = ch;
    } else if (ch == ';') {
      terminator_found = true;
      break;
    } else if (ch == '#') {
      terminator_found = true;
      break;
    }
  }

  // Buffer this line for later.
  if (!terminator_found && it->require_terminator) return false;

  size_t end = cursor;
  if (terminator_found) end--;
  // This happens if the line was not terminated.
  if (end == t.len) end--;
  while (end > start && is_whitespace(t.content[end])) end--;

  it->cursor = cursor;
  it->current = (struct u8_slice){.content = t.content + start, .len = 1 + end - start};

  int line_count = 0;
  for (; c0 < cursor; c0++)
    if (t.content[c0] == '\n') line_count++;
  it->line_count = line_count;
  return true;
}

bool velvet_cmd_arg_iterator_unget(struct velvet_cmd_arg_iterator *it) {
  if (it->current.content) {
    char *base = (char*)it->src.content;
    size_t new_cursor = (char*)it->current.content - base;
    if (new_cursor > 0) new_cursor--;
    it->cursor = new_cursor;
    it->current = (struct u8_slice){0};
    return true;
  }
  return false;
}

bool velvet_cmd_arg_iterator_next(struct velvet_cmd_arg_iterator *it) {
  struct u8_slice t = it->src;
  size_t start = it->cursor;
  size_t cursor = start;

  while (start < t.len && is_whitespace(t.content[start])) start++;
  if (start >= t.len) return false;

  uint8_t lead = t.content[start];
  uint8_t terminator = lead == '\'' ? '\'' : lead == '"' ? '"' : ' ';
  if (terminator != ' ') start++;

  bool escape = false;
  for (cursor = start; cursor < t.len; cursor++) {
    char ch = t.content[cursor];
    if (!escape && ch == '\\') {
      escape = true;
    } else if (escape) {
      escape = false;
    } else if (ch == terminator) {
      break;
    } else if (ch == ';' && !terminator) {
      break;
    }
  }

  size_t end = cursor - 1; // don't include the terminating character
  if (cursor < t.len) cursor++;
  if (terminator == ' ') {
    // trim whitespace if the word was not quoted
    while (end > start && is_whitespace(t.content[end])) end--;
  }
  while (cursor < t.len && is_whitespace(t.content[cursor])) cursor++;
  it->cursor = cursor;
  it->current = (struct u8_slice){.content = t.content + start, .len = 1 + end - start};
  return true;
}

struct velvet_action_data {
  struct velvet *v;
  struct string cmd;
};

static int l_socket_print(lua_State *L) {
  int source_socket = lua_tointeger(L, lua_upvalueindex(1));
  if (source_socket == 0) return 0;
  int n = lua_gettop(L);
  for (int i = 1; i <= n; i++) {
    size_t len = 0;
    const char *s = luaL_tolstring(L, i, &len);
    if (i > 1) write(source_socket, "\t", 1);
    write(source_socket, s, len);
    lua_pop(L, 1);
  }
  write(source_socket, "\n", 1);
  return 0;
}

static void velvet_lua(struct velvet *v, struct u8_slice chunk, int source_socket) {
  lua_State *L = v->L;

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
    velvet_lua(v, chunk, source_socket);
  }
}
