#ifndef velvet_scene_H
#define velvet_scene_H

#include "collections.h"
#include "vte_host.h"
#include "platform.h"

struct host_features {
  bool synchronized_rendering;
};

struct velvet_scene {
  struct vec /*vte_host*/ hosts;
  struct platform_winsize ws;
  size_t focus;
  uint8_t prefix;
  struct string draw_buffer;
  struct host_features host_features;
};

static const struct velvet_scene velvet_scene_default = {.prefix = ('x' & 037), .hosts = vec(struct vte_host)};

void velvet_scene_spawn_process(struct velvet_scene *m, char *process);
void velvet_scene_remove_exited(struct velvet_scene *m);
void velvet_scene_resize(struct velvet_scene *m, struct platform_winsize w);
void velvet_scene_arrange(struct velvet_scene *m);
void velvet_scene_destroy(struct velvet_scene *m);
void velvet_scene_set_focus(struct velvet_scene *m, size_t focus);

typedef void (render_func_t)(struct u8_slice str, void *context);
void velvet_scene_render(struct velvet_scene *m, render_func_t *render_func, bool full_redraw, void *context);

#endif // velvet_scene_H
