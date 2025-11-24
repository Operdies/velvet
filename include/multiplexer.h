#ifndef MULTIPLEXER_H
#define MULTIPLEXER_H

#include "collections.h"
#include "vte_host.h"

struct multiplexer {
  struct vec /* vte_host */ clients;
  int rows, columns;
  size_t focus;
  uint8_t prefix;
};

static const struct multiplexer multiplexer_default = {.clients = vec(struct vte_host), .prefix = ('B' & 037 /* CTRL-B */)};

void multiplexer_feed_input(struct multiplexer *m, uint8_t *input, int n);
void multiplexer_spawn_process(struct multiplexer *m, char *process);
void multiplexer_remove_exited(struct multiplexer *m);
void multiplexer_resize(struct multiplexer *m, int rows, int columns);
void multiplexer_arrange(struct multiplexer *m);

#endif // MULTIPLEXER_H
