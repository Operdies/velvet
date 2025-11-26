#ifndef MULTIPLEXER_H
#define MULTIPLEXER_H

#include "collections.h"
#include "vte_host.h"

struct multiplexer {
  struct vec /*vte_host*/ hosts;
  int rows, columns;
  size_t focus;
  uint8_t prefix;
  struct string draw_buffer;
};

static const struct multiplexer multiplexer_default = {.hosts = vec(struct vte_host),
                                                       .prefix = ('x' & 037 /* CTRL-B */)};

void multiplexer_feed_input(struct multiplexer *m, uint8_t *input, int n);
void multiplexer_spawn_process(struct multiplexer *m, char *process);
void multiplexer_remove_exited(struct multiplexer *m);
void multiplexer_resize(struct multiplexer *m, int rows, int columns);
void multiplexer_arrange(struct multiplexer *m);
void multiplexer_destroy(struct multiplexer *m);

typedef void (render_func_t)(const uint8_t * const buffer, size_t n, void *context);
void multiplexer_render(struct multiplexer *m, render_func_t *render_func, void *context);

#endif // MULTIPLEXER_H
