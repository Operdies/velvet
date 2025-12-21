#ifndef PTY_HOST_H
#define PTY_HOST_H

#include "platform.h"
#include "vte.h"
#include "collections.h"

struct pty_host {
  struct string cmdline;
  struct string title;
  struct string icon;
  struct string cwd;
  int pty, pid;
  int border_width;
  struct {
    struct rect window;
    struct rect client;
  } rect;
  bool dragging;
  struct vte emulator;
};

void pty_host_destroy(struct pty_host *pty_host);
void pty_host_resize(struct pty_host *pty_host, struct rect window);
void pty_host_start(struct pty_host *pty_host);
void pty_host_process_output(struct pty_host *pty_host, struct u8_slice str);
void pty_host_update_cwd(struct pty_host *p);
void pty_host_notify_focus(struct pty_host *p, bool focused);

#endif /*  PTY_HOST_H */
