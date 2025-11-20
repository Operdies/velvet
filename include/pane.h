#ifndef PANE_H
#define PANE_H

#include "vte.h"
#include "collections.h"

struct bounds {
  int x, y, w, h;
};

struct pane {
  // TODO: scrollback
  char *process;
  char title[256];
  char startwd[256];
  char cwd[256];
  int pty, pid;
  int logfile;
  int border_width;
  bool border_dirty, has_focus;
  struct {
    struct bounds window;
    struct bounds client;
  } rect;
  struct pane *next;
  struct vte vte;
};

extern struct pane *clients;
extern struct pane *focused;

void pane_destroy(struct pane *pane);
void pane_resize(struct pane *pane, struct bounds outer);
void pane_start(struct pane *pane);
struct pane *pane_from_pid(struct pane *p, int pid);
struct pane *pane_from_pty(struct pane *lst, int pty);
void pane_process_output(struct pane *pane, uint8_t *buf, int n);
int pane_count(struct pane *pane);
void pane_remove(struct pane **lst, struct pane *rem);
void pane_draw(struct pane *pane, bool redraw, struct string *buffer);
void pane_draw_border(struct pane *p, struct string *b);
void pane_update_cwd(struct pane *p);
void pane_notify_focus(struct pane *p, bool focused);

#endif /*  PANE_H */
