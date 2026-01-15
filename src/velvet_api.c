#include "lauxlib.h"
#include "velvet.h"
#include <string.h>
#include <sys/stat.h>
#include "velvet_api.h"

lua_Integer vv_api_get_key_repeat_timeout(struct velvet *v) {
  return v->input.options.key_repeat_timeout_ms;
}
lua_Integer vv_api_set_key_repeat_timeout(struct velvet *v, lua_Integer new_value) {
  v->input.options.key_repeat_timeout_ms = new_value;
  return v->input.options.key_repeat_timeout_ms;
}

static void lua_bail(lua_State *L, char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void lua_bail(lua_State *L, char *fmt, ...) {
  va_list ap;
  va_start(ap);
  lua_pushvfstring(L, fmt, ap);
  va_end(ap);
  lua_error(L);
}

lua_Integer vv_api_window_create_process(struct velvet *v, const char* cmd) {
  struct velvet_window template = {
    .emulator = vte_default,
    .border_width = 1,
  };

  string_push_cstr(&template.cmdline, cmd);
  return (lua_Integer)velvet_scene_spawn_process_from_template(&v->scene, template);
}

void vv_api_session_detach(struct velvet *v, lua_Integer session_id) {
  struct velvet_session *s;
  vec_find(s, v->sessions, s->socket == session_id);
  if (!s) lua_bail(v->L, "No session exists with socket id %lld", session_id);
  velvet_detach_session(v, s);
}

void vv_api_window_close(struct velvet *v, lua_Integer winid) {
  struct velvet_window *w = velvet_scene_get_window_from_id(&v->scene, winid);
  if (w) {
    velvet_scene_close_and_remove_window(&v->scene, w);
    velvet_ensure_render_scheduled(v);
  }
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
    struct rect r = w->rect.window;
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
    velvet_window_resize(w, new_geometry);
    velvet_ensure_render_scheduled(v);
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

struct velvet_api_terminal_geometry vv_api_get_terminal_geometry(struct velvet *v) {
  struct velvet_api_terminal_geometry geom = { .height = v->scene.ws.h, .width = v->scene.ws.w };
  return geom;
}

static void pcall_func_ref(lua_State *L, lua_Integer func_ref) {
  lua_rawgeti(L, LUA_REGISTRYINDEX, func_ref);

  if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
    const char *err = lua_tostring(L, -1);
    velvet_log("lua error: %s", err);
    lua_pop(L, 1);
  }
}

struct schedule_object {
  struct velvet *v;
  lua_Integer func;
};

void schedule_execute(void *data) {
  struct schedule_object *o = data;
  pcall_func_ref(o->v->L, o->func);
  luaL_unref(o->v->L, LUA_REGISTRYINDEX, o->func);
  free(o);
}

void vv_api_schedule_after(struct velvet *v, lua_Integer delay, lua_Integer func) {
  luaL_checktype(v->L, func, LUA_TFUNCTION);
  lua_pushvalue(v->L, func);
  lua_Integer ref = luaL_ref(v->L, LUA_REGISTRYINDEX);
  struct schedule_object *o = velvet_calloc(1, sizeof(*o));
  o->func = ref;
  o->v = v;
  io_schedule(&v->event_loop, delay, schedule_execute, o);
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

void vv_api_window_send_text(struct velvet *v, lua_Integer winid, const char* text) {
  struct u8_slice t = u8_slice_from_cstr(text);
  velvet_input_put_text(v, t, winid);
}

void vv_api_window_send_keys(struct velvet *v, lua_Integer winid, const char* keys) {
  struct u8_slice t = u8_slice_from_cstr(keys);
  velvet_input_put_keys(v, t, winid);
}

void vv_api_set_focused_window(struct velvet *v, lua_Integer winid){
  struct velvet_window *w = velvet_scene_get_window_from_id(&v->scene, winid);
  if (w) velvet_scene_set_focus(&v->scene, w);
}

void vv_api_swap_windows(struct velvet *v, lua_Integer first, lua_Integer second) {
  struct velvet_window *w1 = velvet_scene_get_window_from_id(&v->scene, first);
  struct velvet_window *w2 = velvet_scene_get_window_from_id(&v->scene, second);
  if (w1 && w2) {
    lua_Integer i1 = vec_index(&v->scene.windows, w1);
    lua_Integer i2 = vec_index(&v->scene.windows, w2);
    vec_swap(&v->scene.windows, i1, i2);
  }
}

static enum velvet_key_modifier modifier_from_slice(struct u8_slice s) {
  if (u8_match(s, "alt") || u8_match(s, "meta") || u8_match(s, "M")) return MODIFIER_ALT;
  if (u8_match(s, "control") || u8_match(s, "C")) return MODIFIER_CTRL;
  if (u8_match(s, "super") || u8_match(s, "D")) return MODIFIER_SUPER;
  return 0;
}

void vv_api_keymap_remap_modifier(struct velvet *v, const char *from, const char *to) {
  enum velvet_key_modifier f, t;
  struct u8_slice f1, t1;
  f1 = u8_slice_from_cstr(from);
  t1 = u8_slice_from_cstr(to);
  f = modifier_from_slice(f1);
  t = modifier_from_slice(t1);

  lua_Integer enum_index[] = { [MODIFIER_ALT] = 0, [MODIFIER_CTRL] = 1, [MODIFIER_SUPER] = 2 };

  if (f && t) {
    v->input.options.modremap[enum_index[f]] = t;
  }
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

const char* vv_api_window_get_title(struct velvet *v, lua_Integer winid) {
  struct velvet_window *w = velvet_scene_get_window_from_id(&v->scene, winid);
  if (!w) lua_bail(v->L, "Window id %lld is not valid.", winid);
  velvet_window_update_title(w);
  string_ensure_null_terminated(&w->title);
  return (char *)w->title.content;
}

void vv_api_window_set_title(struct velvet *v, lua_Integer winid, const char* title) {
  struct velvet_window *w = velvet_scene_get_window_from_id(&v->scene, winid);
  if (!w) lua_bail(v->L, "Window id %lld is not valid.", winid);
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
  if (s == nullptr || !s->output)
    lua_bail(v->L, "Session %lld is not a valid session.", session_id);
  velvet_set_focused_session(v, session_id);
}

lua_Integer vv_api_get_active_session(struct velvet *v) {
  struct velvet_session *s = velvet_get_focused_session(v);
  if (s) return s->socket;
  return 0;
}

void vv_api_server_kill(struct velvet *v) {
  v->quit = true;
}

void vv_api_window_set_hidden(struct velvet *v, lua_Integer winid, bool hidden) {
  struct velvet_window *w = velvet_scene_get_window_from_id(&v->scene, winid);
  if (!w) lua_bail(v->L, "Window id %lld is not valid.", winid);
  if (w->hidden != hidden) {
    w->hidden = hidden;
    velvet_ensure_render_scheduled(v);
  }
}

bool vv_api_window_get_hidden(struct velvet *v, lua_Integer winid) {
  struct velvet_window *w = velvet_scene_get_window_from_id(&v->scene, winid);
  if (!w) lua_bail(v->L, "Window id %lld is not valid.", winid);
  return w->hidden;
}

void vv_api_window_set_z_index(struct velvet *v, lua_Integer win, lua_Integer z) {
  struct velvet_window *w = velvet_scene_get_window_from_id(&v->scene, win);
  if (!w) lua_bail(v->L, "Window id %lld is not valid.", win);
  if (w->z_index != z) {
    w->z_index = z;
    velvet_ensure_render_scheduled(v);
  }
}
lua_Integer vv_api_window_get_z_index(struct velvet *v, lua_Integer win) {
  struct velvet_window *w = velvet_scene_get_window_from_id(&v->scene, win);
  if (!w) lua_bail(v->L, "Window id %lld is not valid.", win);
  return w->z_index;
}

float vv_api_window_get_opacity(struct velvet *v, lua_Integer win) {
  struct velvet_window *w = velvet_scene_get_window_from_id(&v->scene, win);
  if (!w) lua_bail(v->L, "Window id %lld is not valid.", win);
  return 1.0 - w->transparency.alpha;
}
void vv_api_window_set_opacity(struct velvet *v, lua_Integer win, float opacity) {
  struct velvet_window *w = velvet_scene_get_window_from_id(&v->scene, win);
  if (!w) lua_bail(v->L, "Window id %lld is not valid.", win);
  opacity = CLAMP(opacity, 0, 1);
  w->transparency.alpha = 1.0 - opacity;
  velvet_ensure_render_scheduled(v);
}
const char* vv_api_window_get_transparency_mode(struct velvet *v, lua_Integer win) {
  struct velvet_window *w = velvet_scene_get_window_from_id(&v->scene, win);
  if (!w) lua_bail(v->L, "Window id %lld is not valid.", win);
  switch (w->transparency.mode) {
  case PSEUDOTRANSPARENCY_OFF: return "off";
  case PSEUDOTRANSPARENCY_CLEAR: return "clear";
  case PSEUDOTRANSPARENCY_ALL: return "all";
  }
}
void vv_api_window_set_transparency_mode(struct velvet *v, lua_Integer win, const char *mode) {
  struct velvet_window *w = velvet_scene_get_window_from_id(&v->scene, win);
  if (!w) lua_bail(v->L, "Window id %lld is not valid.", win);

  if (strcmp(mode, "off") == 0)
    w->transparency.mode = PSEUDOTRANSPARENCY_OFF;
  else if (strcmp(mode, "clear") == 0)
    w->transparency.mode = PSEUDOTRANSPARENCY_CLEAR;
  else if (strcmp(mode, "all") == 0)
    w->transparency.mode = PSEUDOTRANSPARENCY_ALL;
  else
    lua_bail(v->L, "Transparency mode %s is not valid.", mode);
  velvet_ensure_render_scheduled(v);
}
