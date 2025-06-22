#include <assert.h>
#include <errno.h>
#include <pane.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
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
    kill(pane->pid, SIGTERM);
    pid_t result = waitpid(pane->pid, &status, WNOHANG);
    if (result == -1) die("waitpid:");
  }
  pane->pty = 0;
  pane->pid = 0;
  fsm_destroy(&pane->fsm);
  if (pane->logfile > 0) close(pane->logfile);
  free(pane->process);
}

struct pane *pane_from_pty(struct pane *p, int pty) {
  for (; p; p = p->next)
    if (p->pty == pty) return p;
  return nullptr;
}

void pane_draw(struct pane *pane, bool redraw, struct string *outbuffer) {
  static uint8_t fg, bg;
  static uint32_t attr;

  {
    // Ensure the grid content is in sync with the pane just-in-time
    pane->fsm.w = pane->w;
    pane->fsm.h = pane->h;
    fsm_grid_resize(&pane->fsm);
  }

  struct grid *g = pane->fsm.active_grid;
  char fmt[100];
  for (int i0 = 0; i0 < g->h; i0++) {
    int row = (i0 + g->offset) % g->h;
    struct cell *line = &g->cells[row * g->w];
    if (!redraw && !line->dirty) continue;
    line->dirty = false;
    int lineno = 1 + pane->y + i0;
    int columnno = 1 + pane->x;
    int line_length = MIN(line->n_significant, g->w);
    // TODO: Get rid of snprintf since it's really slow
    int n = snprintf(fmt, sizeof(fmt), "\x1b[%d;%dH", lineno, columnno);
    string_push(outbuffer, fmt, n);

    for (int col = 0; col < line_length; col++) {
      struct cell *c = &line[col];
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
      string_push(outbuffer, (char *)c->symbol.utf8, c->symbol.len);
    }

    int num_blanks = g->w - line_length;
    if (num_blanks > 0) string_memset(outbuffer, ' ', num_blanks);
  }
}

static void on_report_mouse_position(void *context, int row, int col) {
  struct pane *p = context;
  if (p->pty) {
    char buf[30];
    int n = snprintf(buf, sizeof(buf), "\x1b[%d;%dR", row, col);
    write(p->pty, buf, n);
  }
}

void pane_write(struct pane *pane, uint8_t *buf, int n) {
  // Pass current size information to fsm so it can determine if grids should
  // be resized
  pane->fsm.w = pane->w;
  pane->fsm.h = pane->h;
  pane->fsm.on_report_cursor_position = on_report_mouse_position;
  pane->fsm.context = pane;
  if (pane->logfile > 0)
    write(pane->logfile, buf, n);
  fsm_process(&pane->fsm, buf, n);
}

void pane_resize(struct pane *pane, int w, int h) {
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
  pane->pid = pid;
  char buf[100];
  int n = snprintf(buf, sizeof(buf), "%s_log.ansi", pane->process);
  buf[n] = 0;
  int fd = open(buf, O_CREAT | O_CLOEXEC | O_RDWR | O_TRUNC, 0644);
  pane->logfile = fd;
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
