#ifndef PANE_H
#define PANE_H

#include "emulator.h"
#include "collections.h"

struct bounds {
  int x, y, w, h;
};

struct pane {
  // TODO: scrollback
  char *process;
  int pty, pid;
  int logfile;
  int border_width;
  bool border_dirty, has_focus;
  struct {
    struct bounds window;
    struct bounds client;
  } rect;
  struct pane *next;
  struct fsm fsm;
};

extern struct pane *clients;
extern struct pane *focused;

void pane_destroy(struct pane *pane);
void pane_resize(struct pane *pane, struct bounds outer);
void pane_start(struct pane *pane);
struct pane *pane_from_pty(struct pane *lst, int pty);
void pane_write(struct pane *pane, uint8_t *buf, int n);
int pane_count(struct pane *pane);
void pane_remove(struct pane **lst, struct pane *rem);
void pane_draw(struct pane *pane, bool redraw, struct string *buffer);
void pane_draw_border(struct pane *p, struct string *b);

#endif /*  PANE_H */
