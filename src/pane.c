#include <assert.h>
#include <pane.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/ttycom.h>
#include <termios.h>
#include <unistd.h>

#include "collections.h"
#include "emulator.h"
#include "utils.h"

// forkpty from 'util.h' on mac, pty.h on linux
extern pid_t forkpty(int *, char *, struct termios *, struct winsize *);

void pane_destroy(struct pane *pane) {
  if (pane->pty) close(pane->pty);
  if (pane->pid) {
    int status;
    pid_t result = waitpid(pane->pid, &status, WNOHANG);
    if (result == -1) die("waitpid:");
  }
  pane->pty = 0;
  pane->pid = 0;
  free(pane->process);
  free(pane);
}

struct pane *pane_from_pty(struct pane *p, int pty) {
  for (; p; p = p->next)
    if (p->pty == pty) return p;
  return nullptr;
}

void pane_draw(struct pane *pane, bool redraw) {
  static struct string outbuffer = {0};
  static uint8_t fg, bg;
  static uint32_t attr;
  struct grid *g = pane->fsm.opts.alternate_screen ? &pane->fsm.alternate : &pane->fsm.primary;
  uint8_t fmt[100];
  string_clear(&outbuffer);
  for (int i0 = 0; i0 < g->h; i0++) {
    int row = (i0 + g->offset) % g->h;
    if (!redraw && !g->dirty[row]) continue;
    g->dirty[row] = false;
    int lineno = 1 + pane->y + i0;
    int columnno = 1 + pane->x;
    int n = sprintf((char *)fmt, "\x1b[%d;%dH", lineno, columnno);
    string_push(&outbuffer, fmt, n);

    for (int j = 0; j < g->w; j++) {
      struct cell *c = &g->cells[row * g->w + j];
      if (c->fg != fg) {
        // TODO: apply fg
        fg = c->fg;
      }
      if (c->bg != bg) {
        // TODO: apply bg
        bg = c->bg;
      }
      if (c->attr != attr) {
        // TODO: apply attributes
        attr = c->attr;
      }
      string_push(&outbuffer, c->symbol.utf8, c->symbol.len);
    }
  }
  write(STDOUT_FILENO, outbuffer.content, outbuffer.len);
}

void pane_read(struct pane *pane, bool *exit) {
  static const int max_read = 1 << 16;
  static unsigned char *buf;
  if (!buf) buf = calloc(1, max_read);
  // Read at most `max_read` bytes.
  // This is to avoid a spammy application from choking the main loop.
  // If the application really wants to send another `max_read` bytes immediately it will be scheduled after other panes
  // have been processed
  int n = read(pane->pty, buf, max_read);
  if (n > 0) {
    // Pass current size information to fsm so it can determine if grids should be resized
    pane->fsm.w = pane->w;
    pane->fsm.h = pane->h;
    fsm_process(&pane->fsm, buf, n);
  }

  if (n == 0) {
    // stdout closed
    *exit = true;
  }
}

void pane_resize(struct pane *pane, int w, int h) {
  int debugsize = 10;
  if (debugsize) w = h = debugsize;
  if (pane->w != w || pane->h != h) {
    struct winsize ws = {.ws_col = w, .ws_row = h};
    if (pane->pty) ioctl(pane->pty, TIOCSWINSZ, &ws);
    if (pane->pid) kill(pane->pid, SIGWINCH);
    pane->w = w;
    pane->h = h;
  }
}

void pane_start(struct pane *pane) {
  struct winsize panesize = {.ws_col = pane->w, .ws_row = pane->h};
  pid_t pid = forkpty(&pane->pty, NULL, NULL, &panesize);
  if (pid < 0) die("forkpty:");

  if (pid == 0) {
    execlp(pane->process, pane->process, NULL);
    die("execlp:");
  }
}

void pane_remove(struct pane **lst, struct pane *rem) {
  assert(lst), assert(*lst), assert(rem);
  if (*lst == rem) {
    *lst = rem->next;
    return;
  }
  struct pane *prev = *lst;
  for (struct pane *p = prev->next; p; p = p->next) {
    if (p == rem) {
      prev->next = p->next;
    }
    prev = p;
  }
  // struct pane **assign = lst;
  // for (struct pane *p = *lst; p; assign = &p->next, p = p->next) {
  //   if (p == rem) {
  //     *assign = p->next;
  //     return;
  //   }
  // }
}

int pane_count(struct pane *pane) {
  int n = 0;
  for (; pane; pane = pane->next) n++;
  return n;
}
