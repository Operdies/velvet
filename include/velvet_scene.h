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

struct velvet_scene_renderer {
  int w, h;
  struct screen_cell *cells;
  struct velvet_scene_renderer_line *lines;
  struct string draw_buffer;
  struct screen_cell_style current_style;
  struct cursor_options current_cursor;
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
  struct platform_winsize ws;
  size_t focus;
  uint8_t prefix;
  struct velvet_scene_renderer renderer;
  struct velvet_scene_style style;
};

static const struct velvet_scene velvet_scene_default = {.prefix = ('x' & 037), .hosts = vec(struct pty_host)};

void velvet_scene_spawn_process(struct velvet_scene *m, struct u8_slice cmdline);
void velvet_scene_remove_exited(struct velvet_scene *m);
void velvet_scene_resize(struct velvet_scene *m, struct platform_winsize w);
void velvet_scene_arrange(struct velvet_scene *m);
void velvet_scene_destroy(struct velvet_scene *m);
void velvet_scene_set_focus(struct velvet_scene *m, size_t focus);

typedef void (render_func_t)(struct u8_slice str, void *context);
void velvet_scene_render(struct velvet_scene *m, render_func_t *render_func, bool full_redraw, void *context);

#endif // VELVET_SCENE_H
