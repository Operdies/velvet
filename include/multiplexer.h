#ifndef MULTIPLEXER_H
#define MULTIPLEXER_H

#include "collections.h"
#include "vte_host.h"
#include "platform.h"

struct host_features {
  bool synchronized_rendering;
};

struct multiplexer {
  struct vec /*vte_host*/ hosts;
  struct platform_winsize ws;
  size_t focus;
  uint8_t prefix;
  struct string draw_buffer;
  struct host_features host_features;
};

static const struct multiplexer multiplexer_default = {.prefix = ('x' & 037), .hosts = vec(struct vte_host)};

void multiplexer_feed_input(struct multiplexer *m, struct u8_slice str);
void multiplexer_spawn_process(struct multiplexer *m, char *process);
void multiplexer_remove_exited(struct multiplexer *m);
void multiplexer_resize(struct multiplexer *m, struct platform_winsize w);
void multiplexer_arrange(struct multiplexer *m);
void multiplexer_destroy(struct multiplexer *m);
void multiplexer_set_focus(struct multiplexer *m, size_t focus);

typedef void (render_func_t)(struct u8_slice str, void *context);
void multiplexer_render(struct multiplexer *m, render_func_t *render_func, void *context);

#endif // MULTIPLEXER_H
