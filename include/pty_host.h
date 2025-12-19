#ifndef PTY_HOST_H
#define PTY_HOST_H

#include "vte.h"
#include "collections.h"

struct bounds {
  int x, y, columns, lines, x_pixel, y_pixel;
};

struct pty_host {
  struct string cmdline;
  struct string title;
  struct string icon;
  struct string cwd;
  int pty, pid;
  int border_width;
  struct {
    struct bounds window;
    struct bounds client;
  } rect;
  struct vte emulator;
};

void pty_host_destroy(struct pty_host *pty_host);
void pty_host_resize(struct pty_host *pty_host, struct bounds outer);
void pty_host_start(struct pty_host *pty_host);
void pty_host_process_output(struct pty_host *pty_host, struct u8_slice str);
void pty_host_update_cwd(struct pty_host *p);
void pty_host_notify_focus(struct pty_host *p, bool focused);

#endif /*  PTY_HOST_H */
