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
  struct string draw_buffer;
  struct host_features host_features;
};

static const struct multiplexer multiplexer_default = {.hosts = vec(struct vte_host)};

void multiplexer_spawn_process(struct multiplexer *m, char *process);
void multiplexer_remove_exited(struct multiplexer *m);
void multiplexer_resize(struct multiplexer *m, struct platform_winsize w);
void multiplexer_arrange(struct multiplexer *m);
void multiplexer_destroy(struct multiplexer *m);
void multiplexer_set_focus(struct multiplexer *m, size_t focus);
void multiplexer_swap_next(struct multiplexer *m);
void multiplexer_focus_next(struct multiplexer *m);
void multiplexer_focus_previous(struct multiplexer *m);
void multiplexer_zoom(struct multiplexer *m);
void multiplexer_swap_previous(struct multiplexer *m);
void multiplexer_decnmaster(struct multiplexer *m);
void multiplexer_incnmaster(struct multiplexer *m);
void multiplexer_decfactor(struct multiplexer *m);
void multiplexer_incfactor(struct multiplexer *m);

typedef void (render_func_t)(struct u8_slice str, void *context);
void multiplexer_render(struct multiplexer *m, render_func_t *render_func, bool full_redraw, void *context);

#endif // MULTIPLEXER_H
