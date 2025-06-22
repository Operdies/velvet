#ifndef PANE_H
#define PANE_H

#include "emulator.h"
#include "collections.h"

struct pane {
  // TODO: scrollback
  char *process;
  int pty, pid;
  int logfile;
  int x, y, w, h;
  struct pane *next;
  struct fsm fsm;
};

void pane_destroy(struct pane *pane);
void pane_resize(struct pane *pane, int w, int h);
void pane_start(struct pane *pane);
struct pane *pane_from_pty(struct pane *lst, int pty);
void pane_write(struct pane *pane, uint8_t *buf, int n);
int pane_count(struct pane *pane);
void pane_remove(struct pane **lst, struct pane *rem);
void pane_draw(struct pane *pane, bool redraw, struct string *buffer);

#endif /*  PANE_H */
