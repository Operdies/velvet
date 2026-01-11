#include "lauxlib.h"
#include "velvet.h"
#include <sys/stat.h>
#include "velvet_api.h"

int vv_api_get_key_repeat_timeout(struct velvet *v) {
  return v->input.options.key_repeat_timeout_ms;
}
int vv_api_set_key_repeat_timeout(struct velvet *v, int new_value) {
  v->input.options.key_repeat_timeout_ms = new_value;
  return v->input.options.key_repeat_timeout_ms;
}

int vv_api_get_view(struct velvet *v) {
  return v->scene.view;
}
int vv_api_set_view(struct velvet *v, int new_value) {
  velvet_scene_set_view(&v->scene, new_value);
  return v->scene.view;
}
int vv_api_get_tags(struct velvet *vv, int winid){
  return velvet_scene_get_tags_for_window(&vv->scene, winid);
}
int vv_api_set_tags(struct velvet *vv, int tags, int winid){
  velvet_scene_set_tags_for_window(&vv->scene, winid, tags);
  return vv_api_get_tags(vv, winid);
}

static bool layer_from_string(const char *layer, enum velvet_scene_layer *out) {
  struct u8_slice S = u8_slice_from_cstr(layer);
  if (u8_match(S, "floating")) {
    *out = VELVET_LAYER_FLOATING;
    return true;
  } else if (u8_match(S, "tiled")) {
    *out = VELVET_LAYER_TILED;
    return true;
  } else if (u8_match(S, "background")) {
    *out = VELVET_LAYER_BACKGROUND;
    return true;
  }
  return false;
}

int vv_api_spawn(struct velvet *vv, const char* cmd, int* left, int* top, int* width, int* height, const char** layer) {
  struct rect initial_size = {
    .x = left ? *left : 10,
    .y = top ? *top : 5,
    .w = width ? *width : 80,
    .h = height ? *height : 24,
  };
  struct velvet_window template = {
    .emulator = vte_default,
    .border_width = 1,
    .layer = VELVET_LAYER_TILED,
    .rect.window = initial_size,
  };
  string_push_cstr(&template.cmdline, cmd);

  if (layer && *layer && layer_from_string(*layer, &template.layer)) {
    layer_from_string(*layer, &template.layer);
  }
  return (int)velvet_scene_spawn_process_from_template(&vv->scene, template);
}

void vv_api_detach(struct velvet *vv, int *session_id) {
  struct velvet_session *s;
  if (session_id) {
    vec_find(s, vv->sessions, s->socket == *session_id);
  } else {
    s = velvet_get_focused_session(vv);
  }
  if (s) {
    velvet_detach_session(vv, s);
  }
}

void vv_api_close_window(struct velvet *v, int winid, bool force) {
  struct velvet_window *w = velvet_scene_get_window_from_id(&v->scene, winid);
  if (w) velvet_scene_kill_window(&v->scene, w, force);
}

int vv_api_get_focused_window(struct velvet *v){
  struct velvet_window *w = velvet_scene_get_focus(&v->scene);
  return w ? w->id : 0;
}

struct velvet_api_window_geometry vv_api_get_window_geometry(struct velvet *v, int winid){
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

void vv_api_set_window_geometry(struct velvet *v, int winid, struct velvet_api_window_geometry geometry) {
  struct velvet_window *w;
  vec_find(w, v->scene.windows, w->id == winid);
  if (w) {
    struct rect new_geometry = {.h = geometry.height, .y = geometry.top, .x = geometry.left, .w = geometry.width};
    w->layer = VELVET_LAYER_FLOATING;
    velvet_window_resize(w, new_geometry);
  }
}

bool vv_api_is_window_valid(struct velvet *v, int winid) {
  struct velvet_window *w;
  vec_find(w, v->scene.windows, w->id == winid);
  return w ? true : false;
}

int vv_api_list_windows(lua_State *L) {
  struct velvet *vv = *(struct velvet **)lua_getextraspace(L);
  lua_newtable(L);
  int index = 1;
  struct velvet_window *w;
  vec_foreach(w, vv->scene.windows) {
    /* internal windows -- hide them from the lua API for now, and get rid of them later */
    if (w->id < 1000) continue;
    lua_pushinteger(L, w->id);
    lua_seti(L, -2, index++);
  }
  return 1;
}

struct velvet_api_terminal_geometry vv_api_get_terminal_geometry(struct velvet *v) {
  struct velvet_api_terminal_geometry geom = { .height = v->scene.ws.h, .width = v->scene.ws.w };
  return geom;
}

static void pcall_func_ref(lua_State *L, int func_ref) {
  lua_rawgeti(L, LUA_REGISTRYINDEX, func_ref);

  if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
    const char *err = lua_tostring(L, -1);
    velvet_log("lua error: %s", err);
    lua_pop(L, 1);
  }
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

void vv_api_keymap_set(struct velvet *v, const char *keys, int function, bool *repeatable) {
  struct u8_slice chords = u8_slice_from_cstr(keys);
  struct velvet_keymap *added = velvet_keymap_map(v->input.keymap->root, chords);
  if (added) {
    bool rep = repeatable ? *repeatable : false;
    added->is_repeatable = rep;
    /* create a gc handle. This handle is cleared when the keymap is removed */
    luaL_checktype(v->L, function, LUA_TFUNCTION);
    lua_pushvalue(v->L, function);
    int ref = luaL_ref(v->L, LUA_REGISTRYINDEX);
    added->data = (void *)(int64_t)ref;
    added->on_key = keymap_execute;
  }
}

void vv_api_keymap_del(struct velvet *v, const char* keys) {
  struct u8_slice chords = u8_slice_from_cstr(keys);
  velvet_keymap_unmap(v->input.keymap->root, chords);
}

const char *vv_api_get_layer(struct velvet *v, int winid) {
  struct velvet_window *w = velvet_scene_get_window_from_id(&v->scene, winid);
  if (w) {
    switch (w->layer) {
    case VELVET_LAYER_BACKGROUND: return "background";
    case VELVET_LAYER_STATUS: return "status";
    case VELVET_LAYER_TILED: return "tiled";
    case VELVET_LAYER_FLOATING: return "floating";
    case VELVET_LAYER_NOTIFICATION: return "notification";
    default: break;
    }
  }
  return nullptr;
}

void vv_api_set_layer(struct velvet *v, int winid, const char* layer) {
  struct velvet_window *w = velvet_scene_get_window_from_id(&v->scene, winid);
  enum velvet_scene_layer new_layer;
  if (w) {
    if (layer_from_string(layer, &new_layer))
      w->layer = new_layer;
  }
}

bool vv_api_get_display_damage(struct velvet *v) {
  return v->scene.renderer.options.display_damage;
}
bool vv_api_set_display_damage(struct velvet *v, bool new_value) {
  velvet_scene_set_display_damage(&v->scene, new_value);
  return v->scene.renderer.options.display_damage;
}

void vv_api_window_send_text(struct velvet *v, int winid, const char* text) {
  struct u8_slice t = u8_slice_from_cstr(text);
  velvet_input_put_text(v, t, winid);
}

void vv_api_window_send_keys(struct velvet *v, int winid, const char* keys) {
  struct u8_slice t = u8_slice_from_cstr(keys);
  velvet_input_put_keys(v, t, winid);
}

void vv_api_set_focused_window(struct velvet *v, int winid){
  struct velvet_window *w = velvet_scene_get_window_from_id(&v->scene, winid);
  if (w) velvet_scene_set_focus(&v->scene, w);
}

void vv_api_swap_windows(struct velvet *v, int first, int second) {
  struct velvet_window *w1 = velvet_scene_get_window_from_id(&v->scene, first);
  struct velvet_window *w2 = velvet_scene_get_window_from_id(&v->scene, second);
  if (w1 && w2) {
    int i1 = vec_index(&v->scene.windows, w1);
    int i2 = vec_index(&v->scene.windows, w2);
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

  int enum_index[] = { [MODIFIER_ALT] = 0, [MODIFIER_CTRL] = 1, [MODIFIER_SUPER] = 2 };

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
