#ifndef vte_host_H
#define vte_host_H

#include "vte.h"
#include "collections.h"

struct bounds {
  int x, y, w, h;
};

struct vte_host {
  // TODO: scrollback
  char *process;
  char title[50];
  char icon[50];
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
  struct vte_host *next;
  struct vte vte;
};

extern struct vte_host *clients;
extern struct vte_host *focused;

void vte_host_destroy(struct vte_host *vte_host);
void vte_host_resize(struct vte_host *vte_host, struct bounds outer);
void vte_host_start(struct vte_host *vte_host);
struct vte_host *vte_host_from_pid(struct vte_host *p, int pid);
struct vte_host *vte_host_from_pty(struct vte_host *lst, int pty);
void vte_host_process_output(struct vte_host *vte_host, uint8_t *buf, int n);
int vte_host_count(struct vte_host *vte_host);
void vte_host_remove(struct vte_host **lst, struct vte_host *rem);
void vte_host_draw(struct vte_host *vte_host, bool redraw, struct string *buffer);
void vte_host_draw_border(struct vte_host *p, struct string *b);
void vte_host_update_cwd(struct vte_host *p);
void vte_host_notify_focus(struct vte_host *p, bool focused);

#endif /*  vte_host_H */
