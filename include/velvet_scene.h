#ifndef VELVET_SCENE_H
#define VELVET_SCENE_H

#include "collections.h"
#include "platform.h"
#include "vte.h"

#define DAMAGE_MAX 20

enum velvet_scene_layer {
  VELVET_LAYER_BACKGROUND,
  VELVET_LAYER_STATUS,
  VELVET_LAYER_TILED,
  VELVET_LAYER_FLOATING,
  VELVET_LAYER_POPUP,
  VELVET_LAYER_NOTIFICATION,
  VELVET_LAYER_LAST,
};

enum velvet_window_close_when {
  VELVET_WINDOW_CLOSE_ON_EXIT = 0,
  VELVET_WINDOW_CLOSE_ON_ESCAPE = 1,
  VELVET_WINDOW_CLOSE_AFTER_DELAY = 2,
};

enum velvet_window_kind {
  VELVET_WINDOW_PTY_HOST,
  VELVET_WINDOW_HINT,
};

struct velvet_window_close_behavior {
  enum velvet_window_close_when when;
  uint64_t delay_ms;
};

struct velvet_window {
  struct string cmdline;
  struct string title;
  struct string icon;
  struct string cwd;
  int pty, pid;
  uint64_t id;
  int border_width;
  struct {
    struct rect window;
    struct rect client;
    struct rect title;
  } rect;
  uint64_t exited_at;
  struct vte emulator;
  struct velvet_window_close_behavior close;
  enum velvet_window_kind kind;
  enum velvet_scene_layer layer;
  bool dragging;
  uint32_t tags;
};

void velvet_window_destroy(struct velvet_window *velvet_window);
void velvet_window_resize(struct velvet_window *velvet_window, struct rect window);
bool velvet_window_start(struct velvet_window *velvet_window);
void velvet_window_process_output(struct velvet_window *velvet_window, struct u8_slice str);
void velvet_window_update_title(struct velvet_window *p);

struct velvet_render_option {
  /* unfortunately some emulators don't support repeating multi-byte characters.
   * I observed this with the stock emulator on MacOS.
   * If we detect the host emulator of the active session is AppleTerminal, then we disable this feature.
   */
  bool no_repeat_wide_chars;
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
  struct {
    bool enabled;
    float alpha;
  } pseudotransparency;
  struct color palette[16];
  struct {
    struct color visible;
    struct color not_visible;
  } status;
  struct color mantle;
};

struct velvet_render_state_cache {
  /* remember previous state changes to avoid re-transmitting them */
  struct {
    struct cursor position;
    struct cursor_options style;
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
  struct velvet_render_buffer staging_buffer;
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
  struct vec /* int */ focus_order; 
  struct rect ws;
  struct velvet_render renderer;
  struct velvet_theme theme;
  struct velvet_scene_layout layout;
  void (*arrange)(struct velvet_scene* scene);
  uint32_t view;
  uint32_t prev_view;
};

enum velvet_window_hit_location {
  VELVET_HIT_TITLE               = 1,
  VELVET_HIT_CLIENT              = 2,
  VELVET_HIT_BORDER              = 4,
  VELVET_HIT_BORDER_TOP          = 8                        | VELVET_HIT_BORDER,
  VELVET_HIT_BORDER_BOTTOM       = 16                       | VELVET_HIT_BORDER,
  VELVET_HIT_BORDER_LEFT         = 32                       | VELVET_HIT_BORDER,
  VELVET_HIT_BORDER_RIGHT        = 64                       | VELVET_HIT_BORDER,
  VELVET_HIT_BORDER_TOPLEFT      = VELVET_HIT_BORDER_TOP    | VELVET_HIT_BORDER_LEFT,
  VELVET_HIT_BORDER_TOPRIGHT     = VELVET_HIT_BORDER_TOP    | VELVET_HIT_BORDER_RIGHT,
  VELVET_HIT_BORDER_BOTTOM_LEFT  = VELVET_HIT_BORDER_BOTTOM | VELVET_HIT_BORDER_LEFT,
  VELVET_HIT_BORDER_BOTTOM_RIGHT = VELVET_HIT_BORDER_BOTTOM | VELVET_HIT_BORDER_RIGHT,
};

struct velvet_window_hit {
  struct velvet_window *win;
  enum velvet_window_hit_location where;
};

struct velvet_window *velvet_scene_get_window_from_id(struct velvet_scene *m, uint64_t id);
bool velvet_scene_hit(struct velvet_scene *scene, int x, int y, struct velvet_window_hit *hit, bool skip(struct velvet_window*, void*), void *data);
void velvet_scene_set_view(struct velvet_scene *scene, uint32_t view_mask);
void velvet_scene_toggle_view(struct velvet_scene *scene, uint32_t view_mask);
void velvet_scene_set_tags(struct velvet_scene *scene, uint32_t tag_mask);
void velvet_scene_toggle_tags(struct velvet_scene *scene, uint32_t tag_mask);
struct velvet_window * velvet_scene_manage(struct velvet_scene *m, struct velvet_window template);
void velvet_scene_spawn_process_from_template(struct velvet_scene *m, struct velvet_window template);
void velvet_scene_spawn_process(struct velvet_scene *m, struct u8_slice cmdline);
void velvet_scene_remove_window(struct velvet_scene *m, struct velvet_window *w);
void velvet_scene_resize(struct velvet_scene *m, struct rect w);
void velvet_scene_arrange(struct velvet_scene *m);
void velvet_scene_destroy(struct velvet_scene *m);
void velvet_scene_set_focus(struct velvet_scene *m, struct velvet_window *new_focus);
struct velvet_window *velvet_scene_focus_previous(struct velvet_scene *m);
struct velvet_window *velvet_scene_focus_next(struct velvet_scene *m);
void velvet_scene_set_display_damage(struct velvet_scene *m, bool track_damage);
void velvet_scene_draw_tile_hint(struct velvet_scene *m, struct velvet_window *before);

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

/* XTERM palette */
// static const struct color ansi16[16] = {
//     /* 30–37 */
//     RGB("#000000"), // black
//     RGB("#cd0000"), // red
//     RGB("#00cd00"), // green
//     RGB("#cdcd00"), // yellow
//     RGB("#0000ee"), // blue
//     RGB("#cd00cd"), // magenta
//     RGB("#00cdcd"), // cyan
//     RGB("#e5e5e5"), // white (light gray)
//     /* 90–97 */
//     RGB("#7f7f7f"), // bright black (dark gray)
//     RGB("#ff0000"), // bright red
//     RGB("#00ff00"), // bright green
//     RGB("#ffff00"), // bright yellow
//     RGB("#5c5cff"), // bright blue
//     RGB("#ff00ff"), // bright magenta
//     RGB("#00ffff"), // bright cyan
//     RGB("#ffffff"), // bright white
// };

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
    .pseudotransparency =
        {
            .enabled = true,
            .alpha = 0.30f,
        },
    .mantle = RGB("#181825"),
    .status =
        {
            .visible = RGB("#f38ba8"),
            .not_visible = RGB("#b4befe"),
        },
    .palette =
        {
            /* catppuccin-mocha inspired palette */
            /* 30-37 / 40-47 */
            RGB("#45475a"), /* black */
            RGB("#f38ba8"), /* red */
            RGB("#a6e3a1"), /* green */
            RGB("#f9e2af"), /* yellow */
            RGB("#89b4fa"), /* blue */
            RGB("#f5c2e7"), /* magenta */
            RGB("#94e2d5"), /* cyan */
            RGB("#bac2de"), /* white */
            /* 90-97 / 100-107 */
            RGB("#585b70"), /* bright black (gray) */
            RGB("#f38ba8"), /* bright red */
            RGB("#a6e3a1"), /* bright green */
            RGB("#f9e2af"), /* bright yellow */
            RGB("#89b4fa"), /* bright blue */
            RGB("#f5c2e7"), /* bright magenta */
            RGB("#94e2d5"), /* bright cyan */
            RGB("#a6adc8"), /* bright white */
        },
};

static const struct velvet_render_state_cache render_state_cache_invalidated = {
    .cursor.position = {.column = -1, .line = -1},
    .cell.style = {.attr = -1, .bg = {.cmd = -1}, .fg = {.cmd = -1}},
    .cursor.style = {.style = -1, .visible = false},
};

static const struct velvet_scene velvet_scene_default = {
    .windows = vec(struct velvet_window),
    .focus_order = vec(uint64_t),
    .theme = velvet_theme_default,
    .renderer =
        {
            .options =
                {
                    .no_repeat_wide_chars = false,
                    .display_eol = false,
                },
            .state = render_state_cache_invalidated,

        },
    .arrange = velvet_scene_arrange,
    .layout =
        {
            .nmaster = 1,
            .mfact = 0.5f,
            .notification_height = 5,
            .notification_width = 40,
        },
    .view = 1,
    .prev_view = 1,
};

#endif // VELVET_SCENE_H
