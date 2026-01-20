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

static void
velvet_cmd_set_option(struct velvet *v, struct velvet_session *sender, struct u8_slice option, struct u8_slice value) {
  /* TODO: This sucks. The implementation mixes:
   * 1. Changing settings on the *active session*
   * 2. Changing settings on the *sender* (ephemeral CLI, active session, socat, ....)
   * 3. Changing settings on various server-side subsystems
   * A common interface to change settings is fine, but mixing all three in a huge if-else is not great */

  struct velvet_session *focus = velvet_get_focused_session(v);

  int digit = 0;
  bool is_digit = u8_slice_digit(value, &digit);
  bool boolean = digit || (value.len && value.content[0] == 't');
  if (u8_match(option, "lines")) {
    if (focus && is_digit) focus->ws.h = digit;
  } else if (u8_match(option, "columns")) {
    if (focus && is_digit) focus->ws.w = digit;
  } else if (u8_match(option, "lines_pixels")) {
    if (focus && is_digit) focus->ws.y_pixel = digit;
  } else if (u8_match(option, "columns_pixels")) {
    if (focus && is_digit) focus->ws.x_pixel = digit;
  } else if (u8_match(option, "cwd")) {
    if (sender) {
      string_clear(&sender->cwd);
      string_push_slice(&sender->cwd, value);
    }
  } else if (u8_match(option, "no_repeat_wide_chars")) {
    focus->features.no_repeat_wide_chars = boolean;
  }
}

static void velvet_cmd_set(struct velvet *v, struct velvet_session *sender, struct velvet_cmd_arg_iterator *it) {
  struct u8_slice option, value;
  if (velvet_cmd_arg_iterator_next(it)) {
    option = it->current;
    if (velvet_cmd_arg_iterator_next(it)) {
      value = it->current;
      velvet_cmd_set_option(v, sender, option, value);
    }
  }
}


static int l_velvet_lua_print_to_socket(lua_State *L) {
  int source_socket = luaL_checkinteger(L, 1);
  if (source_socket == 0) return 0;
  size_t len = 0;
  const char *str = luaL_checklstring(L, 2, &len);
  write(source_socket, str, len);
  return 0;
}

static void velvet_lua(struct velvet *v, struct velvet_cmd_arg_iterator *it, int source_socket) {
  if (!velvet_cmd_arg_iterator_next(it)) {
    velvet_log("lua: missing string");
    return;
  }
  struct u8_slice cmd = it->current;
  lua_State *L = v->L;
  lua_pushcfunction(L, l_velvet_lua_print_to_socket);
  lua_setglobal(L, "lua_hack_print_to_socket");

  struct string S = {0};
  string_push_format_slow(&S,
                          "local func = function() %.*s end\n"
                          "local global_print = print\n"
                          "print = function(x) lua_hack_print_to_socket(%d, vv.inspect(x) .. '\\n') end\n"
                          "local ok, err = pcall(func)\n"
                          "if not ok then print(err) end\n"
                          "print = global_print\n",
                          (int)cmd.len,
                          cmd.content,
                          source_socket);

  if (luaL_loadbuffer(v->L, (char*)S.content, S.len, nullptr) != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK) {
    size_t len = 0;
    const char *err = lua_tolstring(L, -1, &len);
    if (source_socket) {
      write(source_socket, err, len);
    } else {
      velvet_log("lua cmd error: %s", err);
    }
  }
  lua_pop(L, lua_gettop(L));
  string_destroy(&S);
}

void velvet_cmd(struct velvet *v, int source_socket, struct u8_slice cmd) {
  struct velvet_session *sender = nullptr;
  velvet_log("velvet_cmd: %.*s", (int)cmd.len, cmd.content);
  if (source_socket) vec_find(sender, v->sessions, sender->socket == source_socket);

  if (cmd.len > 2) {
    if (cmd.content[0] == cmd.content[cmd.len - 1]) {
      if (cmd.content[0] == '\'' || cmd.content[0] == '"') {
        /* if `cmd` is a single quoted string, it could be composed of multiple commands.
         * Unquote and resplit the command, and then dispatch them separately.*/
        struct velvet_cmd_iterator it = {.src = u8_slice_range(cmd, 1, -2)};
        while (velvet_cmd_iterator_next(&it)) velvet_cmd(v, source_socket, it.current);
        return;
      }
    }
  }

  struct velvet_cmd_arg_iterator it = {.src = cmd};
  struct u8_slice command;

  struct velvet_scene *s = &v->scene;

  if (velvet_cmd_arg_iterator_next(&it)) {
    struct velvet_window dummy = {0};
    struct velvet_window *focused = velvet_scene_get_focus(s);
    /* avoid null checks when nothing is focused */
    if (!focused) focused = &dummy;
    command = it.current;
    if (u8_match(command, "detach")) {
      velvet_detach_session(v, velvet_get_focused_session(v));
    } else if (u8_match(command, "lua")) {
      velvet_lua(v, &it, source_socket);
    } else if (u8_match(command, "quit")) {
      v->quit = true;
    } else if (u8_match(command, "set")) {
      velvet_cmd_set(v, sender, &it);
    } else {
      velvet_log("Unknown command '%.*s'", (int)command.len, command.content);
    }

    while (velvet_cmd_arg_iterator_next(&it)) {
      velvet_log("Unhandled trailing argument: %.*s", (int)it.current.len, it.current.content);
    }
  }
}
