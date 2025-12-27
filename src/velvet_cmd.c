#include "velvet_cmd.h"
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

static bool velvet_cmd_arg_iterator_rest(struct velvet_cmd_arg_iterator *it) {
  if (it->cursor >= it->src.len) return false;

  size_t remaining = it->src.len - it->cursor;
  struct u8_slice rest = { .content = it->src.content + it->cursor, .len = remaining };
  rest = u8_slice_strip_whitespace(rest);
  it->current = rest;
  if (rest.len == 0) return false;
  it->cursor = it->src.len;
  return true;
};

bool velvet_cmd_arg_iterator_unget(struct velvet_cmd_arg_iterator *it) {
  if (it->current.content) {
    char *base = (char*)it->src.content;
    size_t new_cursor = (char*)it->current.content - base;
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

static bool u8_match(struct u8_slice s, char *opt) {
  return u8_slice_equals(s, u8_slice_from_cstr(opt));
}

struct velvet_action_data {
  struct velvet *v;
  struct string cmd;
};

int u8_slice_digit(struct u8_slice s) {
  if (s.len == 0) return 0;
  size_t i, v, sign;
  i = v = 0;
  sign = 1;
  if (s.content[i] == '-') sign = -1, i++;
  for (; i < s.len; i++) {
    uint8_t ch = s.content[i];
    if (!(ch >= '0' && ch <= '9')) return 0;
    v *= 10;
    v += ch - '0';
  }
  return v * sign;
}

static void
velvet_cmd_set_option(struct velvet *v, struct velvet_session *source, struct u8_slice option, struct u8_slice value) {
  if (!source || source->input == 0) {
    // the sender is not a client, so we should apply this change to the focused session if possible
    source = velvet_get_focused_session(v);
  }
  int digit = u8_slice_digit(value);
  bool boolean = digit || (value.len && value.content[0] == 't');
  if (u8_match(option, "lines")) {
    if (source) source->ws.h = digit;
  } else if (u8_match(option, "columns")) {
    if (source) source->ws.w = digit;
  } else if (u8_match(option, "lines_pixels")) {
    if (source) source->ws.y_pixel = digit;
  } else if (u8_match(option, "columns_pixels")) {
    if (source) source->ws.x_pixel = digit;
  } else if (u8_match(option, "key_repeat_timeout_ms")) {
    v->input.options.key_repeat_timeout_ms = digit;
  } else if (u8_match(option, "key_chain_timeout_ms")) {
    v->input.options.key_chain_timeout_ms = digit;
  } else if (u8_match(option, "focus_follows_mouse")) {
    v->input.options.focus_follows_mouse = boolean;
  } else if (u8_match(option, "layer")) {
    struct velvet_window *window = velvet_scene_get_focus(&v->scene);
    if (u8_match(value, "floating")) {
      window->layer = VELVET_LAYER_FLOATING;
    } else if (u8_match(value, "tiled")) {
      window->layer = VELVET_LAYER_TILED;
    }
  } else if (u8_match(option, "display_damage")) {
    velvet_scene_set_display_damage(&v->scene, boolean);
  } else if (u8_match(option, "no_repeat_wide_chars")) {
    source->features.no_repeat_wide_chars = boolean;
  }
}

static void velvet_action_callback(struct velvet_keymap *k, struct velvet_key_event e) {
  struct velvet_action_data *d = k->data;
  if (e.removed) {
    string_destroy(&d->cmd);
    free(d);
  } else {
    velvet_cmd(d->v, 0, string_as_u8_slice(d->cmd));
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

static void velvet_cmd_map(struct velvet *v, struct velvet_cmd_arg_iterator *it) {
  struct u8_slice keys, map_cmd;
  bool repeat = false;
  if (!velvet_cmd_arg_iterator_next(it)) {
    velvet_log("map: missing parameters");
    return;
  }
  keys = it->current;
  if (u8_match(keys, "--allow-repeat") || u8_match(keys, "-r")) {
    repeat = true;
    if (!velvet_cmd_arg_iterator_next(it)) {
      velvet_log("map: missing parameters");
      return;
    }
    keys = it->current;
  }
  if (!velvet_cmd_arg_iterator_rest(it)) {
    velvet_log("map: missing command");
    return;
  }

  map_cmd = it->current;
  struct velvet_keymap *added = velvet_keymap_map(v->input.keymap->root, keys);
  if (added) {
    struct velvet_action_data *data = calloc(1, sizeof(*data));
    data->v = v;
    added->data = data;
    added->on_key = velvet_action_callback;
    added->is_repeatable = repeat;
    string_push_slice(&data->cmd, map_cmd);
    velvet_log("map %.*s to %.*s", (int)keys.len, keys.content, (int)data->cmd.len, data->cmd.content);
  } else {
    velvet_log("unable to add keymap  %.*s", (int)keys.len, keys.content);
  }
}

static void velvet_cmd_put(struct velvet *v, struct velvet_cmd_arg_iterator *it) {
  struct u8_slice keys;
  if (!velvet_cmd_arg_iterator_rest(it)) {
    velvet_log("`put` missing keys.");
    return;
  }
  keys = it->current;
  velvet_input_put(v, keys);
}

struct window_create_options {
  struct velvet_window_close_behavior close;
  int border_width;
  enum velvet_window_kind kind;
  enum velvet_scene_layer layer;
};


static void velvet_cmd_create_window(struct velvet *v, struct velvet_cmd_arg_iterator *it, struct window_create_options o) {
  struct u8_slice title = {0};
  struct u8_slice cmdline = {0};

  if (!velvet_cmd_arg_iterator_next(it)) return;
  if (u8_match(it->current, "--title")) {
    if (!velvet_cmd_arg_iterator_next(it))
      return;
    title = it->current;
    if (!velvet_cmd_arg_iterator_next(it))
      return;
  }

  velvet_cmd_arg_iterator_unget(it);
  velvet_cmd_arg_iterator_rest(it);
  cmdline = it->current;

  struct velvet_window notification = {
      .border_width = o.border_width,
      .emulator = vte_default,
      .layer = o.layer,
      .close = o.close,
      .kind = o.kind,
  };

  string_push_slice(&notification.cmdline, cmdline);
  if (title.len) {
    string_push_slice(&notification.emulator.osc.title, title);
  }
  velvet_scene_spawn_process_from_template(&v->scene, notification);
}

static void velvet_cmd_spawn_notification(struct velvet *v, struct velvet_cmd_arg_iterator *it) {
  struct window_create_options o = {.border_width = 1,
                                    .close = {.when = VELVET_WINDOW_CLOSE_AFTER_DELAY, .delay_ms = 1500},
                                    .layer = VELVET_LAYER_NOTIFICATION};
  velvet_cmd_create_window(v, it, o);
}

static void velvet_cmd_spawn_tiled(struct velvet *v, struct velvet_cmd_arg_iterator *it) {
  struct window_create_options o = {.border_width = 1, .layer = VELVET_LAYER_TILED};
  velvet_cmd_create_window(v, it, o);
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
    } else if (u8_match(command, "put")) {
      velvet_cmd_put(v, &it);
    } else if (u8_match(command, "decfactor")) {
      s->layout.mfact = MAX(s->layout.mfact - 0.05, 0.05);
    } else if (u8_match(command, "incfactor")) {
      s->layout.mfact = MIN(s->layout.mfact + 0.05, 0.95);
    } else if (u8_match(command, "decnmaster")) {
      s->layout.nmaster = MAX(s->layout.nmaster - 1, 0);
    } else if (u8_match(command, "incnmaster")) {
      s->layout.nmaster = MIN(s->layout.nmaster + 1, 5);
    } else if (u8_match(command, "incborder")) {
      int max_border = MIN(focused->rect.window.h, focused->rect.window.w) / 2;
      focused->border_width = MIN(focused->border_width + 1, max_border);
    } else if (u8_match(command, "decborder")) {
      focused->border_width = MAX(focused->border_width - 1, 0);
    } else if (u8_match(command, "map")) {
      velvet_cmd_map(v, &it);
    } else if (u8_match(command, "spawn")) {
      velvet_cmd_spawn_tiled(v, &it);
    } else if (u8_match(command, "unmap")) {
      struct u8_slice keys;
      if (velvet_cmd_arg_iterator_next(&it)) {
        keys = it.current;
        velvet_log("unmap %.*s", (int)keys.len, keys.content);
        velvet_keymap_unmap(v->input.keymap->root, keys);
      } else {
        velvet_log("`unmap' command missing `keys' parameter.");
      }
    } else if (u8_match(command, "notify")) {
      velvet_cmd_spawn_notification(v, &it);
    } else if (u8_match(command, "focus-next")) {
      velvet_scene_set_focus(&v->scene, (v->scene.focus + 1) % v->scene.windows.length);
    } else if (u8_match(command, "focus-previous")) {
      velvet_scene_set_focus(&v->scene, (v->scene.focus + v->scene.windows.length - 1) % v->scene.windows.length);
    } else if (u8_match(command, "swap-next")) {
      vec_swap(&v->scene.windows, v->scene.focus, (v->scene.focus + 1) % v->scene.windows.length);
      velvet_scene_set_focus(&v->scene, (v->scene.focus + 1) % v->scene.windows.length);
    } else if (u8_match(command, "swap-previous")) {
      vec_swap(&v->scene.windows, v->scene.focus, (v->scene.focus + v->scene.windows.length - 1) % v->scene.windows.length);
      velvet_scene_set_focus(&v->scene, (v->scene.focus + v->scene.windows.length - 1) % v->scene.windows.length);
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
