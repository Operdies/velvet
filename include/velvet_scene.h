#ifndef VELVET_SCENE_H
#define VELVET_SCENE_H

#include "collections.h"
#include "platform.h"
#include "vte.h"

#define DAMAGE_MAX 20

/* We use 4 buffers for debugging damage tracking across draws.
 * In release builds, we only need two buffers. */
#ifdef VELVET_RENDERER_EXTENDED_DAMAGE_DISPLAY
#define N_BUFFERS 4
#else
#define N_BUFFERS 2
#endif

enum velvet_scene_layer {
  VELVET_LAYER_BACKGROUND,
  VELVET_LAYER_TILED,
  VELVET_LAYER_FLOATING,
  VELVET_LAYER_POPUP,
  VELVET_LAYER_DRAGGING,
  VELVET_LAYER_NOTIFICATION,
  VELVET_LAYER_LAST,
};

struct velvet_window {
  struct string cmdline;
  struct string title;
  struct string icon;
  struct string cwd;
  int pty, pid;
  int border_width;
  struct {
    struct rect window;
    struct rect client;
  } rect;
  struct vte emulator;
  enum velvet_scene_layer layer;
};

void velvet_window_destroy(struct velvet_window *velvet_window);
void velvet_window_resize(struct velvet_window *velvet_window, struct rect window);
void velvet_window_start(struct velvet_window *velvet_window);
void velvet_window_process_output(struct velvet_window *velvet_window, struct u8_slice str);
void velvet_window_update_title(struct velvet_window *p);
void velvet_window_notify_focus(struct velvet_window *p, bool focused);

struct velvet_render_option {
  /* unfortunately some emulators don't support repeating multi-byte characters.
   * I observed this with the stock emulator on MacOS.
   * If we detect the host emulator of the active session is AppleTerminal, then we disable this feature.
   */
  bool no_repeat_wide_chars;
  /* debugging option for highlighting changed regions */
  bool display_damage;
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

struct velvet_render {
  int w, h;
  /* multiple buffers used for damage tracking over time */
  struct velvet_render_buffer buffers[N_BUFFERS];
  int current_buffer;
  struct string draw_buffer;
  struct screen_cell_style current_style;
  struct cursor_options current_cursor;
  struct cursor cursor;
  struct velvet_render_option options;
};

struct velvet_scene_style {
  struct {
    struct screen_cell_style title, outline;
  } inactive;
  struct {
    struct screen_cell_style title, outline;
  } active;
};

struct velvet_scene {
  struct vec /*velvet_window*/ hosts;
  struct rect ws;
  size_t focus;
  struct velvet_render renderer;
  struct velvet_scene_style style;
};

#define HEX_TO_NUM(x) (((x) >= '0' && (x) <= '9') ? (x) - '0' : (x) - 'a' + 10)
#define RGB(color)                                                                                                     \
  {.cmd = COLOR_RGB,                                                                                                   \
   .r = (HEX_TO_NUM(color[1]) << 4) | (HEX_TO_NUM(color[2])),                                                          \
   .g = (HEX_TO_NUM(color[3]) << 4) | (HEX_TO_NUM(color[4])),                                                          \
   .b = (HEX_TO_NUM(color[5]) << 4) | (HEX_TO_NUM(color[5]))}

static const struct velvet_scene velvet_scene_default = {
    .hosts = vec(struct velvet_window),
    .style = {.active =
                  {
                      .outline = {.attr = 0, .fg = RGB("#f38ba8"), .bg = RGB("#181825")},
                      .title = {.attr = ATTR_BOLD, .fg = RGB("#f38ba8"), .bg = RGB("#181825")},
                  },
              .inactive =
                  {
                      .outline = {.attr = 0, .fg = RGB("#b4befe"), .bg = RGB("#181825")},
                      .title = {.attr = 0, .fg = RGB("#b4befe"), .bg = RGB("#181825")},
                  }},
    .renderer =
        {
            .options = {.no_repeat_wide_chars = false},
            .cursor = {.column = -1, .line = -1},
        },
};

void velvet_scene_spawn_process_from_template(struct velvet_scene *m, struct velvet_window template);
void velvet_scene_spawn_process(struct velvet_scene *m, struct u8_slice cmdline);
void velvet_scene_remove_exited(struct velvet_scene *m);
void velvet_scene_resize(struct velvet_scene *m, struct rect w);
void velvet_scene_arrange(struct velvet_scene *m);
void velvet_scene_destroy(struct velvet_scene *m);
void velvet_scene_set_focus(struct velvet_scene *m, size_t focus);
void velvet_scene_set_display_damage(struct velvet_scene *m, bool track_damage);

typedef void(render_func_t)(struct u8_slice str, void *context);
void velvet_scene_render_full(struct velvet_scene *m, render_func_t *render_func, void *context);
void velvet_scene_render_damage(struct velvet_scene *m, render_func_t *render_func, void *context);
struct velvet_window *velvet_scene_get_focus(struct velvet_scene *m);

#endif // VELVET_SCENE_H
