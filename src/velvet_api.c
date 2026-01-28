#include "lauxlib.h"
#include "velvet.h"
#include <string.h>
#include <sys/stat.h>
#include "velvet_api.h"
#include "platform.h"
#include <stdarg.h>

_Noreturn static void lua_bail(lua_State *L, char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  lua_pushvfstring(L, fmt, ap);
  va_end(ap);
  lua_error(L);
  /* lua_error longjumps back to lua call site */
  assert(!"Unreachable");
}

lua_Integer vv_api_get_key_repeat_timeout(struct velvet *v) {
  return v->input.options.key_repeat_timeout_ms;
}
lua_Integer vv_api_set_key_repeat_timeout(struct velvet *v, lua_Integer new_value) {
  v->input.options.key_repeat_timeout_ms = new_value;
  return v->input.options.key_repeat_timeout_ms;
}

struct velvet_window *check_window(struct velvet *v, int win) {
  struct velvet_window *w = velvet_scene_get_window_from_id(&v->scene, win);
  if (!w) lua_bail(v->L, "Window id %I is not valid.", win);
  assert(w);
  return w;
}

struct velvet_window *check_process_window(struct velvet *v, int win) {
  struct velvet_window *w = velvet_scene_get_window_from_id(&v->scene, win);
  if (!w) lua_bail(v->L, "Window id %I is not valid.", win);
  if (w->is_lua_window) lua_bail(v->L, "Window id %I is a lua window.", win);
  assert(w);
  return w;
}

lua_Integer vv_api_window_create_process(struct velvet *v, const char *cmd, struct velvet_api_window_create_options options) {
  struct velvet_window template = { .emulator = vte_default };
  string_push_cstr(&template.cmdline, cmd);

  if (options.working_directory.set) string_push_cstr(&template.cwd, options.working_directory.value);
  if (options.parent_window.set) template.parent_window_id = options.parent_window.value;

  return (lua_Integer)velvet_scene_spawn_process_from_template(&v->scene, template);
}

lua_Integer vv_api_window_create(struct velvet *v, struct velvet_api_window_create_options options) {
  struct velvet_window template = {
    .emulator = vte_default,
    .is_lua_window = true,
  };
  if (options.working_directory.set) string_push_cstr(&template.cwd, options.working_directory.value);
  if (options.parent_window.set) template.parent_window_id = options.parent_window.value;
  struct velvet_window *created = velvet_scene_manage(&v->scene, template);
  string_push_format_slow(&created->emulator.osc.title, "Naked window %d", created->id);
  return created->id;
}

bool vv_api_window_is_lua(struct velvet *v, lua_Integer win_id) {
  struct velvet_window *w = velvet_scene_get_window_from_id(&v->scene, win_id);
  if (w) return w->is_lua_window;
  return false;
}

void vv_api_window_write(struct velvet *v, lua_Integer win_id, const char* text) {
  struct velvet_window *w = check_window(v, win_id);
  if (w && w->is_lua_window) {
    if (!w->is_lua_window) lua_bail(v->L, "Window %I is not a lua window.", win_id);
    if (w->geometry.h == 0 || w->geometry.w == 0) lua_bail(v->L, "Cannot write to window: size is 0");
    struct u8_slice s = u8_slice_from_cstr(text);
    velvet_window_process_output(w, s);
    if (window_visible(v, w))
      v->render_invalidated = true;
  }
}

void vv_api_session_detach(struct velvet *v, lua_Integer session_id) {
  struct velvet_session *s;
  vec_find(s, v->sessions, s->socket == session_id);
  if (!s) lua_bail(v->L, "No session exists with socket id %I", session_id);
  velvet_detach_session(v, s);
}

void vv_api_window_close(struct velvet *v, lua_Integer winid) {
  struct velvet_window *w = check_window(v, winid);
  velvet_scene_close_and_remove_window(&v->scene, w);
}

lua_Integer vv_api_get_focused_window(struct velvet *v){
  struct velvet_window *w = velvet_scene_get_focus(&v->scene);
  return w ? w->id : 0;
}

struct velvet_api_window_geometry vv_api_window_get_geometry(struct velvet *v, lua_Integer winid){
  struct velvet_api_window_geometry geom = {0};
  struct velvet_window *w;
  vec_find(w, v->scene.windows, w->id == winid);
  if (w) {
    struct rect r = w->geometry;
    geom.height = r.h;
    geom.left = r.x;
    geom.top = r.y;
    geom.width = r.w;
  }
  return geom;
}

void vv_api_window_set_geometry(struct velvet *v, lua_Integer winid, struct velvet_api_window_geometry geometry) {
  struct velvet_window *w;
  /* sanity check -- 1000 is already ridiculous, but let's be lenient */
  if (geometry.width < 0 || geometry.width > 1000 || geometry.height < 0 || geometry.height > 1000)
    return;
  vec_find(w, v->scene.windows, w->id == winid);
  if (w) {
    struct rect new_geometry = {.h = geometry.height, .y = geometry.top, .x = geometry.left, .w = geometry.width};
    if (velvet_window_resize(w, new_geometry, v))
      v->render_invalidated = true;
  }
}

bool vv_api_window_is_valid(struct velvet *v, lua_Integer winid) {
  struct velvet_window *w;
  vec_find(w, v->scene.windows, w->id == winid);
  return w ? true : false;
}

lua_Integer vv_api_get_windows(lua_State *L) {
  struct velvet *v = *(struct velvet **)lua_getextraspace(L);
  lua_newtable(L);
  lua_Integer index = 1;
  struct velvet_window *w;
  /* hide non-regular windows from the LUA api for now */
  vec_foreach(w, v->scene.windows) {
    lua_pushinteger(L, w->id);
    lua_seti(L, -2, index++);
  }
  return 1;
}

struct velvet_api_screen_geometry vv_api_get_screen_geometry(struct velvet *v) {
  struct velvet_api_screen_geometry geom = { .height = v->scene.ws.h, .width = v->scene.ws.w };
  return geom;
}

static void pcall_func_ref(lua_State *L, lua_Integer func_ref) {
  lua_rawgeti(L, LUA_REGISTRYINDEX, func_ref);

  if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
    struct velvet *v = *(struct velvet **)lua_getextraspace(L);
    const char *err = lua_tostring(L, -1);
    struct velvet_api_system_message_event_args event_args = {
      .level = VELVET_API_SEVERITY_ERROR,
      .message = err,
    };
    velvet_api_raise_system_message(v, event_args);
    lua_pop(L, 1);
  }
}

static struct velvet *VELVET;
void schedule_execute(void *data) {
  assert(VELVET);
  lua_Integer func = (lua_Integer)data;
  pcall_func_ref(VELVET->L, func);
  luaL_unref(VELVET->L, LUA_REGISTRYINDEX, func);
}

void vv_api_schedule_after(struct velvet *v, lua_Integer delay, lua_Integer func) {
  VELVET = v;
  luaL_checktype(v->L, func, LUA_TFUNCTION);
  lua_pushvalue(v->L, func);
  lua_Integer ref = luaL_ref(v->L, LUA_REGISTRYINDEX);
  io_schedule(&v->event_loop, delay, schedule_execute, (void*)(lua_Integer)ref);
}

static void keymap_execute(struct velvet_keymap *k, struct velvet_key_event evt) {
  struct velvet *v = k->root->data;
  lua_State *L = v->L;
  lua_Integer lua_function = (lua_Integer)(int64_t)k->data;
  if (evt.removed) {
    luaL_unref(L, LUA_REGISTRYINDEX, lua_function);
  } else {
    pcall_func_ref(L, lua_function);
  }
}

void vv_api_keymap_set(struct velvet *v, const char *keys, lua_Integer function, bool *repeatable) {
  struct u8_slice chords = u8_slice_from_cstr(keys);
  struct velvet_keymap *added = velvet_keymap_map(v->input.keymap->root, chords);
  if (added) {
    bool rep = repeatable ? *repeatable : false;
    added->is_repeatable = rep;
    /* create a gc handle. This handle is cleared when the keymap is removed */
    luaL_checktype(v->L, function, LUA_TFUNCTION);
    lua_pushvalue(v->L, function);
    lua_Integer ref = luaL_ref(v->L, LUA_REGISTRYINDEX);
    added->data = (void *)(int64_t)ref;
    added->on_key = keymap_execute;
  }
}

void vv_api_keymap_del(struct velvet *v, const char* keys) {
  struct u8_slice chords = u8_slice_from_cstr(keys);
  velvet_keymap_unmap(v->input.keymap->root, chords);
}

bool vv_api_get_display_damage(struct velvet *v) {
  return v->scene.renderer.options.display_damage;
}
bool vv_api_set_display_damage(struct velvet *v, bool new_value) {
  velvet_scene_set_display_damage(&v->scene, new_value);
  return v->scene.renderer.options.display_damage;
}

void vv_api_window_paste_text(struct velvet *v, lua_Integer winid, const char* text) {
  struct u8_slice t = u8_slice_from_cstr(text);
  velvet_input_paste_text(v, t, winid);
}

void vv_api_window_send_keys(struct velvet *v, lua_Integer winid, const char* keys) {
  struct u8_slice t = u8_slice_from_cstr(keys);
  velvet_input_send_keys(v, t, winid);
}

void vv_api_set_focused_window(struct velvet *v, lua_Integer winid){
  struct velvet_window *w = check_window(v, winid);
  if (w) velvet_scene_set_focus(&v->scene, w);
}

enum velvet_api_key_modifier check_modifier(lua_State *L, enum velvet_api_key_modifier m) {
  switch (m) {
  case VELVET_API_KEY_MODIFIER_ALT: return m;
  case VELVET_API_KEY_MODIFIER_CONTROL: return m;
  case VELVET_API_KEY_MODIFIER_SUPER: return m;
  case VELVET_API_KEY_MODIFIER_SHIFT: lua_bail(L, "Shift cannot be remapped.");
  case VELVET_API_KEY_MODIFIER_HYPER: lua_bail(L, "Hyper cannot be remapped.");
  case VELVET_API_KEY_MODIFIER_META: return VELVET_API_KEY_MODIFIER_ALT;
  case VELVET_API_KEY_MODIFIER_CAPS_LOCK: lua_bail(L, "Caps lock cannot be remapped.");
  case VELVET_API_KEY_MODIFIER_NUM_LOCK: lua_bail(L, "Num lock cannot be remapped."); break;
  default: lua_bail(L, "Multiple modifiers specified. Please specify a single modifier.");
  }

  /* unreachable */
  assert(!"Unreachable");
}

void vv_api_keymap_remap_modifier(struct velvet *v, enum velvet_api_key_modifier from, enum velvet_api_key_modifier to) {
  lua_Integer enum_index[] = { [VELVET_API_KEY_MODIFIER_ALT] = 0, [VELVET_API_KEY_MODIFIER_CONTROL] = 1, [VELVET_API_KEY_MODIFIER_SUPER] = 2 };
  from = check_modifier(v->L, from);
  to = check_modifier(v->L, to);
  v->input.options.modremap[enum_index[from]] = to;
}

bool vv_api_get_focus_follows_mouse(struct velvet *v) {
    return v->input.options.focus_follows_mouse;
}
bool vv_api_set_focus_follows_mouse(struct velvet *v, bool new_value) {
  v->input.options.focus_follows_mouse = new_value;
  return new_value;
}

lua_Integer vv_api_get_current_tick(struct velvet *v) {
  (void)v;
  return get_ms_since_startup();
}

const char* vv_api_window_get_title(struct velvet *v, lua_Integer win_id) {
  struct velvet_window *w = check_window(v, win_id);
  if (w->emulator.osc.title.len) {
    string_ensure_null_terminated(&w->emulator.osc.title);
    return (const char*)w->emulator.osc.title.content;
  }
  if (w->cmdline.len) {
    string_ensure_null_terminated(&w->cmdline);
    return (const char*)w->cmdline.content;
  }
  return NULL;
}

void vv_api_window_set_title(struct velvet *v, lua_Integer win_id, const char* title) {
  struct velvet_window *w = check_window(v, win_id);
  string_clear(&w->emulator.osc.title);
  string_push_cstr(&w->emulator.osc.title, title);
}

lua_Integer vv_api_get_sessions(lua_State *L) {
  struct velvet *v = *(struct velvet **)lua_getextraspace(L);
  lua_newtable(L);
  lua_Integer index = 1;
  struct velvet_session *s;
  vec_where(s, v->sessions, s->socket && s->output) {
    if (s->socket) {
      lua_pushinteger(L, s->socket);
      lua_seti(L, -2, index++);
    }
  }
  return 1;
}

void vv_api_set_active_session(struct velvet *v, lua_Integer session_id) {
  struct velvet_session *s;
  vec_find(s, v->sessions, s->socket == session_id);
  if (s == NULL || !s->output)
    lua_bail(v->L, "Session %I is not a valid session.", session_id);
  velvet_set_focused_session(v, session_id);
}

lua_Integer vv_api_get_active_session(struct velvet *v) {
  struct velvet_session *s = velvet_get_focused_session(v);
  if (s) return s->socket;
  return 0;
}

void vv_api_quit(struct velvet *v) {
  v->quit = true;
}

void vv_api_window_set_hidden(struct velvet *v, lua_Integer win_id, bool hidden) {
  struct velvet_window *w = check_window(v, win_id);
  if (w->hidden != hidden) {
    w->hidden = hidden;
    v->render_invalidated = true;
  }
}

bool vv_api_window_get_hidden(struct velvet *v, lua_Integer win_id) {
  struct velvet_window *w = check_window(v, win_id);
  return w->hidden;
}

void vv_api_window_set_z_index(struct velvet *v, lua_Integer win_id, lua_Integer z) {
  struct velvet_window *w = check_window(v, win_id);
  if (w->z_index != z) {
    w->z_index = z;
    v->render_invalidated = true;
  }
}
lua_Integer vv_api_window_get_z_index(struct velvet *v, lua_Integer win_id) {
  struct velvet_window *w = check_window(v, win_id);
  return w->z_index;
}

float vv_api_window_get_opacity(struct velvet *v, lua_Integer win_id) {
  struct velvet_window *w = check_window(v, win_id);
  return 1.0 - w->transparency.alpha;
}
void vv_api_window_set_opacity(struct velvet *v, lua_Integer win_id, float opacity) {
  struct velvet_window *w = check_window(v, win_id);
  opacity = CLAMP(opacity, 0, 1);
  w->transparency.alpha = 1.0 - opacity;
  v->render_invalidated = true;
}

enum velvet_api_transparency_mode vv_api_window_get_transparency_mode(struct velvet *v, lua_Integer win_id) {
  struct velvet_window *w = check_window(v, win_id);
  return w->transparency.mode;
}

void vv_api_window_set_transparency_mode(struct velvet *v, lua_Integer win_id, enum velvet_api_transparency_mode mode) {
  struct velvet_window *w = check_window(v, win_id);

  switch (mode) {
  case VELVET_API_TRANSPARENCY_MODE_NONE:
  case VELVET_API_TRANSPARENCY_MODE_CLEAR:
  case VELVET_API_TRANSPARENCY_MODE_ALL: w->transparency.mode = mode; break;
  default: lua_bail(v->L, "Invalid transparency mode %I", mode);
  }

  v->render_invalidated = true;
}

static uint8_t fconv(float f) {
  return CLAMP(f * 255, 0, 255);
}
static float iconv(uint8_t v) {
  return (float)v / 255.0f;
}

static struct color rgb_from_palette(struct velvet_api_rgb_color pal) {
  struct color rgb = {
      .kind = COLOR_RGB,
      .red = fconv(pal.red),
      .green = fconv(pal.green),
      .blue = fconv(pal.blue),
      .alpha = fconv(pal.alpha.value),
  };
  return rgb;
}

static struct velvet_api_rgb_color palette_from_rgb(struct color col) {
  struct velvet_api_rgb_color api = {
      .red = iconv(col.red),
      .blue = iconv(col.blue),
      .green = iconv(col.green),
      .alpha.value = iconv(col.alpha),
  };
  return api;
}

struct velvet_api_theme vv_api_get_theme(struct velvet *v) {
  struct velvet_api_theme p = {0};
  p.black = palette_from_rgb(v->scene.theme.palette[0]);
  p.red = palette_from_rgb(v->scene.theme.palette[1]);
  p.green = palette_from_rgb(v->scene.theme.palette[2]);
  p.yellow = palette_from_rgb(v->scene.theme.palette[3]);
  p.blue = palette_from_rgb(v->scene.theme.palette[4]);
  p.magenta = palette_from_rgb(v->scene.theme.palette[5]);
  p.cyan = palette_from_rgb(v->scene.theme.palette[6]);
  p.white = palette_from_rgb(v->scene.theme.palette[7]);
  p.bright_black = palette_from_rgb(v->scene.theme.palette[8]);
  p.bright_red = palette_from_rgb(v->scene.theme.palette[9]);
  p.bright_green = palette_from_rgb(v->scene.theme.palette[10]);
  p.bright_yellow = palette_from_rgb(v->scene.theme.palette[11]);
  p.bright_blue = palette_from_rgb(v->scene.theme.palette[12]);
  p.bright_magenta = palette_from_rgb(v->scene.theme.palette[13]);
  p.bright_cyan = palette_from_rgb(v->scene.theme.palette[14]);
  p.bright_white = palette_from_rgb(v->scene.theme.palette[15]);
  p.foreground = palette_from_rgb(v->scene.theme.foreground);
  p.background = palette_from_rgb(v->scene.theme.background);
  p.cursor_foreground.value = palette_from_rgb(v->scene.theme.cursor.foreground);
  p.cursor_background.value = palette_from_rgb(v->scene.theme.cursor.background);
  return p;
}

struct velvet_api_theme vv_api_set_theme(struct velvet *v, struct velvet_api_theme new_value) {
  v->scene.theme.palette[0] = rgb_from_palette(new_value.black);
  v->scene.theme.palette[1] = rgb_from_palette(new_value.red);
  v->scene.theme.palette[2] = rgb_from_palette(new_value.green);
  v->scene.theme.palette[3] = rgb_from_palette(new_value.yellow);
  v->scene.theme.palette[4] = rgb_from_palette(new_value.blue);
  v->scene.theme.palette[5] = rgb_from_palette(new_value.magenta);
  v->scene.theme.palette[6] = rgb_from_palette(new_value.cyan);
  v->scene.theme.palette[7] = rgb_from_palette(new_value.white);
  v->scene.theme.palette[8] = rgb_from_palette(new_value.bright_black);
  v->scene.theme.palette[9] = rgb_from_palette(new_value.bright_red);
  v->scene.theme.palette[10] = rgb_from_palette(new_value.bright_green);
  v->scene.theme.palette[11] = rgb_from_palette(new_value.bright_yellow);
  v->scene.theme.palette[12] = rgb_from_palette(new_value.bright_blue);
  v->scene.theme.palette[13] = rgb_from_palette(new_value.bright_magenta);
  v->scene.theme.palette[14] = rgb_from_palette(new_value.bright_cyan);
  v->scene.theme.palette[15] = rgb_from_palette(new_value.bright_white);
  v->scene.theme.foreground = rgb_from_palette(new_value.foreground);
  v->scene.theme.background = rgb_from_palette(new_value.background);
  if (new_value.cursor_foreground.set) {
    v->scene.theme.cursor.foreground = rgb_from_palette(new_value.cursor_foreground.value);
  } else {
    v->scene.theme.cursor.foreground = rgb_from_palette(new_value.background);
  }
  if (new_value.cursor_background.set) {
    v->scene.theme.cursor.background = rgb_from_palette(new_value.cursor_background.value);
  } else {
    v->scene.theme.cursor.background = rgb_from_palette(new_value.foreground);
  }
  v->render_invalidated = true;
  return vv_api_get_theme(v);
}

float vv_api_window_get_dim_factor(struct velvet *v, lua_Integer win_id) {
  struct velvet_window *w = check_window(v, win_id);
  return w->dim_factor;
}
void vv_api_window_set_dim_factor(struct velvet *v, lua_Integer win_id, float factor) {
  struct velvet_window *w = check_window(v, win_id);
  w->dim_factor = CLAMP(factor, 0, 1);
  v->render_invalidated = true;
}

void vv_api_window_send_mouse_move(struct velvet *v, struct velvet_api_mouse_move_event_args mouse_move) {
  check_window(v, mouse_move.win_id);
  velvet_input_send_mouse_move(v, mouse_move);
}

void vv_api_window_send_mouse_click(struct velvet *v, struct velvet_api_mouse_click_event_args mouse_click) {
  check_window(v, mouse_click.win_id);
  velvet_input_send_mouse_click(v, mouse_click);
}

void vv_api_window_send_mouse_scroll(struct velvet *v, struct velvet_api_mouse_scroll_event_args mouse_scroll) {
  check_window(v, mouse_scroll.win_id);
  velvet_input_send_mouse_scroll(v, mouse_scroll);
}

lua_Integer vv_api_window_get_scrollback_size(struct velvet *v, lua_Integer win_id) {
  struct velvet_window *w = check_window(v, win_id);
  struct screen *active = vte_get_current_screen(&w->emulator);
  return active->scroll.height;
}
lua_Integer vv_api_window_get_scroll_offset(struct velvet *v, lua_Integer win_id){
  struct velvet_window *w = check_window(v, win_id);
  struct screen *active = vte_get_current_screen(&w->emulator);
  return active->scroll.view_offset;
}

void vv_api_window_set_drawing_color(struct velvet *v, lua_Integer win_id, enum velvet_api_brush brush, struct velvet_api_rgb_color color) {
  struct velvet_window *w = check_window(v, win_id);
  struct color col = rgb_from_palette(color);
  struct screen *g = vte_get_current_screen(&w->emulator);
  switch (brush) {
  case VELVET_API_BRUSH_BACKGROUND: g->cursor.brush.bg = col; break;
  case VELVET_API_BRUSH_FOREGROUND: g->cursor.brush.fg = col; break;
  }
}

void vv_api_window_set_cursor_position(struct velvet *v, lua_Integer win_id, struct velvet_api_coordinate pos) {
  struct velvet_window *w = check_window(v, win_id);
  struct screen *g = vte_get_current_screen(&w->emulator);
  pos.col = CLAMP(pos.col, 1, w->geometry.w);
  pos.row = CLAMP(pos.row, 1, w->geometry.h);

  if (w->emulator.options.cursor.visible && (pos.col != g->cursor.column || pos.row != g->cursor.line))
    v->render_invalidated = true;

  screen_set_cursor_position(g, pos.col - 1, pos.row - 1);
}

const char* vv_api_window_get_working_directory(struct velvet *v, lua_Integer win_id) {
  struct velvet_window *w = check_window(v, win_id);
  if (w->pty && platform.get_cwd_from_pty) {
    char buf[256] = {0};
    if (platform.get_cwd_from_pty(w->pty, buf, sizeof(buf))) {
      string_clear(&w->cwd);
      string_push_cstr(&w->cwd, buf);
    }
  }
  if (w->cwd.len == 0) return NULL;
  string_ensure_null_terminated(&w->cwd);
  return (const char*)w->cwd.content;
}

static char get_process_foreground_buffer[256] = {0};
const char* vv_api_window_get_foreground_process(struct velvet *v, lua_Integer win_id) {
  struct velvet_window *w = check_process_window(v, win_id);
  if (w->pty && platform.get_process_from_pty) {
    if (platform.get_process_from_pty(w->pty, get_process_foreground_buffer, sizeof(get_process_foreground_buffer))) {
      return get_process_foreground_buffer;
    }
  }
  return NULL;
}

lua_Integer vv_api_window_get_parent(struct velvet *v, lua_Integer win_id) {
  struct velvet_window *w = check_window(v, win_id);
  return w->parent_window_id;
}

const char* vv_api_get_startup_directory(struct velvet *v) {
  return v->startup_directory;
}
