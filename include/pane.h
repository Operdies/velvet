#ifndef PANE_H
#define PANE_H

#include <sys/syslimits.h>
#include "emulator.h"
#include "collections.h"

struct pane {
  // TODO: scrollback
  char *process;
  int pty, pid;
  int x, y, w, h;
  struct pane *next;
  struct fsm fsm;
};

void pane_destroy(struct pane *pane);
void pane_resize(struct pane *pane, int w, int h);
void pane_start(struct pane *pane);
struct pane *pane_from_pty(struct pane *lst, int pty);
void pane_read(struct pane *pane, bool *exit);
int pane_count(struct pane *pane);
void pane_remove(struct pane **lst, struct pane *rem);
void pane_draw(struct pane *pane, bool redraw, struct string *buffer);

#endif /*  PANE_H */
