#ifndef VELVET_SCENE_H
#define VELVET_SCENE_H

#include "collections.h"
#include "pty_host.h"
#include "platform.h"

struct velvet_scene_renderer_line {
  int cell_offset;
  struct {
    /* [start..end) is an inclusive range specifying what needs to be rendered */
    int start, end;
  } damage;
};

struct velvet_scene_renderer_options {
  /* unfortunately some emulators don't support repeating multi-byte characters.
   * I observed this with the stock emulator on MacOS.
   * If we detect the host emulator of the active session is AppleTerminal, then we disable this feature.
   */
  bool no_repeat_wide_chars;
};

struct velvet_scene_renderer {
  int w, h;
  struct screen_cell *cells;
  struct velvet_scene_renderer_line *lines;
  struct string draw_buffer;
  struct screen_cell_style current_style;
  struct cursor_options current_cursor;
  struct cursor cursor;
  struct velvet_scene_renderer_options options;
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
  struct vec /*pty_host*/ hosts;
  struct rect ws;
  size_t _focus;
  struct velvet_scene_renderer renderer;
  struct velvet_scene_style style;
};

#define HEX_TO_NUM(x) (((x) >= '0' && (x) <= '9') ? (x) - '0' : (x) - 'a' + 10)
#define RGB(color)                                                                                                     \
  {.cmd = COLOR_RGB,                                                                                                   \
   .r = (HEX_TO_NUM(color[1]) << 4) | (HEX_TO_NUM(color[2])),                                                          \
   .g = (HEX_TO_NUM(color[3]) << 4) | (HEX_TO_NUM(color[4])),                                                          \
   .b = (HEX_TO_NUM(color[5]) << 4) | (HEX_TO_NUM(color[5]))}

static const struct velvet_scene velvet_scene_default = {
    .hosts = vec(struct pty_host),
    .style = {.active =
                  {
                      .outline = {.attr = 0, .fg = RGB("#f38ba8")},
                      .title = {.attr = ATTR_BOLD, .fg = RGB("#f38ba8")},
                  },
              .inactive =
                  {
                      .outline = {.attr = 0, .fg = RGB("#b4befe")},
                      .title = {.attr = 0, .fg = RGB("#b4befe")},
                  }},
    .renderer =
        {
            .options = {.no_repeat_wide_chars = false},
            .cursor = {.column = -1, .line = -1},
        },
};

#undef RGB
#undef HEX_TO_NUM

void velvet_scene_spawn_process(struct velvet_scene *m, struct u8_slice cmdline);
void velvet_scene_remove_exited(struct velvet_scene *m);
void velvet_scene_resize(struct velvet_scene *m, struct rect w);
void velvet_scene_arrange(struct velvet_scene *m);
void velvet_scene_destroy(struct velvet_scene *m);
void velvet_scene_set_focus(struct velvet_scene *m, size_t focus);

typedef void (render_func_t)(struct u8_slice str, void *context);
void velvet_scene_render_full(struct velvet_scene *m, render_func_t *render_func, void *context);
void velvet_scene_render_damage(struct velvet_scene *m, render_func_t *render_func, void *context);
struct pty_host *velvet_scene_get_focus(struct velvet_scene *m);

#endif // VELVET_SCENE_H
