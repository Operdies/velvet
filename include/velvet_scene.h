#ifndef VELVET_SCENE_H
#define VELVET_SCENE_H

#include "collections.h"
#include "vte.h"
#include "velvet_api.h"

#define DAMAGE_MAX 20

struct pseudotransparency_options {
  enum velvet_api_transparency_mode mode;
  float alpha;
};

struct velvet_window {
  struct string cmdline;
  struct string title;
  struct string icon;
  struct string cwd;
  bool is_lua_window;
  int pty, pid;
  int id;
  int z_index;
  bool hidden;
  struct rect geometry;
  uint64_t exited_at;
  struct vte emulator;
  struct pseudotransparency_options transparency;
  float dim_factor;
};

void velvet_window_resize(struct velvet_window *velvet_window, struct rect window, struct velvet *v);
bool velvet_window_start(struct velvet_window *velvet_window);
void velvet_window_process_output(struct velvet_window *velvet_window, struct u8_slice str);
void velvet_window_update_title(struct velvet_window *p);

struct velvet_render_option {
  /* unfortunately some emulators don't support repeating multi-byte characters.
   * I observed this with the stock emulator on MacOS.
   * If we detect the host emulator of the active session is AppleTerminal, then we disable this feature.
   */
  bool no_repeat_multibyte_symbols;
  /* debugging option for highlighting changed regions */
  bool display_damage;
  /* debugging option for highlighting line ends */
  bool display_eol;
};

struct velvet_render_buffer_line {
  struct screen_cell *cells;
  struct {
    int start, end;
  } damage[DAMAGE_MAX];
  int n_damage;
};

struct velvet_render_buffer {
  struct screen_cell *cells;
  struct velvet_render_buffer_line *lines;
};

struct velvet_theme {
  struct {
    struct color active;
    struct color inactive;
  } title;
  struct color background;
  struct color foreground;
  struct {
    struct color foreground;
    struct color background;
  } cursor;
  struct color palette[16];
};

struct velvet_render_state_cache {
  /* remember previous state changes to avoid re-transmitting them */
  struct {
    struct cursor position;
    struct cursor_options options;
  } cursor;
  struct {
    struct screen_cell_style style;
  } cell;
};

struct velvet_render {
  int w, h;
  /* multiple buffers used for damage tracking over time */
  struct velvet_render_buffer buffers[4];
  /* the staging buffer is a temporary buffer which must be explicitly comitted to the
   * current render buffer. This is useful for rendering a layer and then comitting it with effects applied such as
   * alpha blending etc. */
  struct {
    struct velvet_render_buffer buffer;
    /* track the bounding box of the staged area.
     * This should be fairly efficient because we usually stage
     * a single window (rectangular) at a time. */
    int bottom, left, right, top;
  } staged;
  int current_buffer;
  struct string draw_buffer;
  struct velvet_render_option options;
  struct velvet_render_state_cache state;
};

struct velvet_scene_layout {
  int nmaster;
  float mfact;
  int notification_width;
  int notification_height;
};

struct velvet_scene {
  struct vec /*velvet_window*/ windows;
  /* id of window in `windows`. Window can be retrieved with id2win */
  int focus;
  struct rect ws;
  struct velvet_render renderer;
  struct velvet_theme theme;
  struct velvet_scene_layout layout;
  /* needed to raise window creation events. It is a bit spaghetty, but the alternative is just a lot of fuzz for */
  struct velvet *v;
};

struct velvet_window_hit {
  struct velvet_window *win;
};

void velvet_scene_close_and_remove_window(struct velvet_scene *s, struct velvet_window *w);
struct velvet_window *velvet_scene_get_window_from_id(struct velvet_scene *m, int id);
bool velvet_scene_hit(struct velvet_scene *scene, int x, int y, struct velvet_window_hit *hit, bool skip(struct velvet_window*, void*), void *data);
void velvet_scene_set_view(struct velvet_scene *scene, uint32_t view_mask);
void velvet_scene_toggle_view(struct velvet_scene *scene, uint32_t view_mask);
void velvet_scene_set_tags(struct velvet_scene *scene, uint32_t tag_mask);
void velvet_scene_set_tags_for_window(struct velvet_scene *scene, uint64_t winid, uint32_t tag_mask);
uint32_t velvet_scene_get_tags_for_window(struct velvet_scene *scene, uint64_t winid);
void velvet_scene_toggle_tags(struct velvet_scene *scene, uint32_t tag_mask);
struct velvet_window * velvet_scene_manage(struct velvet_scene *m, struct velvet_window template);
int velvet_scene_spawn_process_from_template(struct velvet_scene *m, struct velvet_window template);
int velvet_scene_spawn_process(struct velvet_scene *m, struct u8_slice cmdline);
void velvet_scene_resize(struct velvet_scene *m, struct rect w);
void velvet_scene_arrange(struct velvet_scene *m);
void velvet_scene_destroy(struct velvet_scene *m);
void velvet_scene_set_focus(struct velvet_scene *m, struct velvet_window *new_focus);
void velvet_scene_set_display_damage(struct velvet_scene *m, bool track_damage);

typedef void(render_func_t)(struct u8_slice str, void *context);
void velvet_scene_render_full(struct velvet_scene *m, render_func_t *render_func, void *context);
void velvet_scene_render_damage(struct velvet_scene *m, render_func_t *render_func, void *context);
struct velvet_window *velvet_scene_get_focus(struct velvet_scene *m);

#define HEX_TO_NUM(x) (((x) >= '0' && (x) <= '9') ? (x) - '0' : (x) - 'a' + 10)
#define RGB(rgb)                                                                                                       \
  (struct color) {                                                                                                     \
    .cmd = COLOR_RGB, .r = (HEX_TO_NUM(rgb[1]) << 4) | (HEX_TO_NUM(rgb[2])),                                           \
    .g = (HEX_TO_NUM(rgb[3]) << 4) | (HEX_TO_NUM(rgb[4])), .b = (HEX_TO_NUM(rgb[5]) << 4) | (HEX_TO_NUM(rgb[6]))       \
  }

static const struct velvet_theme velvet_theme_default = {
    .background = RGB("#1e1e2e"),
    .foreground = RGB("#cdd6f4"),
    .cursor =
        {
            .background = RGB("#f5e0dc"),
            .foreground = RGB("#1e1e2e"),
        },
    .title =
        {
            .active = RGB("#f38ba8"),
            .inactive = RGB("#b4befe"),
        },
};

static const struct velvet_render_state_cache render_state_cache_invalidated = {
    .cursor.position = {.column = -1, .line = -1},
    .cell.style = {.attr = -1, .bg = {.kind = -1}, .fg = {.kind = -1}},
    .cursor.options = {.style = -1, .visible = false},
};

static const struct velvet_scene velvet_scene_default = {
    .windows = vec(struct velvet_window),
    .theme = velvet_theme_default,
    .renderer =
        {
            .options =
                {
                    .no_repeat_multibyte_symbols = false,
                    .display_eol = false,
                },
            .state = render_state_cache_invalidated,

        },
    .layout =
        {
            .nmaster = 1,
            .mfact = 0.5f,
            .notification_height = 5,
            .notification_width = 40,
        },
};

#endif // VELVET_SCENE_H
