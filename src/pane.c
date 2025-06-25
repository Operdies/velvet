#include <assert.h>
#include <errno.h>
#include <pane.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

// sprintf is extremely slow because it needs to deal with format strings. We can do better
static const char *move(int row, int col) {
  static char buf[20] = "\x1b[";
  int i_buf = 1;
  char numbuf[12];
  int i_numbuf = 11;
  numbuf[i_numbuf] = 0;
  while (row) {
    i_numbuf--;
    numbuf[i_numbuf] = '0' + row % 10;
    row /= 10;
  }
  for (; numbuf[i_numbuf]; buf[++i_buf] = numbuf[i_numbuf++]);
  buf[++i_buf] = ';';
  while (col) {
    i_numbuf--;
    numbuf[i_numbuf] = '0' + col % 10;
    col /= 10;
  }
  for (; numbuf[i_numbuf]; buf[++i_buf] = numbuf[i_numbuf++]);
  buf[++i_buf] = 'H';
  buf[++i_buf] = 0;

  return buf;
}

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

/* Write the specified style attributes to the outbuffer, but only if they are different from what is currently stored.
 * Note that the use of statics means that this function is not thread safe or re-entrant. It should only be used for
 * streaming content to the screen. */
static inline void apply_style(uint32_t new_attr, struct color new_fg, struct color new_bg, struct string *outbuffer) {
  static struct color fg = color_default;
  static struct color bg = color_default;

  static uint32_t attr;
  (void)fg, (void)bg, (void)attr, (void)new_attr, (void)new_fg, (void)new_bg, (void)outbuffer;

  // 1. Handle attributes
  if (attr != new_attr) {
    if (new_attr == 0) {
      // Unfortunately this also resets colors, so we need to set those again
      fg = bg = color_default;
      string_push(outbuffer, "\x1b[0m");
    } else {
      uint32_t features[] = {
          [0] = ATTR_NONE,
          [1] = ATTR_BOLD,
          [2] = ATTR_FAINT,
          [3] = ATTR_ITALIC,
          [4] = ATTR_UNDERLINE,
          [5] = ATTR_BLINK_SLOW,
          [6] = ATTR_BLINK_RAPID,
          [7] = ATTR_REVERSE,
          [8] = ATTR_CONCEAL,
          [9] = ATTR_CROSSED_OUT,
      };
      for (size_t i = 1; i < LENGTH(features); i++) {
        uint32_t is_on = features[i] & attr;
        uint32_t should_be_on = features[i] & new_attr;
        if (is_on && !should_be_on) {
          if (i == 1) {
            // annoying special case for bold
            char *disable = "\x1b[22m";
            string_push(outbuffer, disable);
          } else {
            char disable[] = {0x1b, '[', '2', '0' + i, 'm', 0};
            string_push(outbuffer, disable);
          }
        } else if (!is_on && should_be_on) {
          char enable[] = {0x1b, '[', '0' + i, 'm', 0};
          string_push(outbuffer, enable);
        }
      }
    }
  }

  attr = new_attr;
  fg = new_fg;
  bg = new_bg;
}

void pane_draw(struct pane *pane, bool redraw, struct string *outbuffer) {

  {
    // Ensure the grid content is in sync with the pane just-in-time
    pane->fsm.w = pane->rect.client.w;
    pane->fsm.h = pane->rect.client.h;
    fsm_grid_resize(&pane->fsm);
  }

  struct grid *g = pane->fsm.active_grid;
  for (int i0 = 0; i0 < g->h; i0++) {
    int row = (i0 + g->offset) % g->h;
    struct grid_row *grid_row = &g->rows[row];
    if (!redraw && !grid_row->dirty) continue;
    grid_row->dirty = false;
    int columnno = 1 + pane->rect.client.x;
    int lineno = 1 + pane->rect.client.y + i0;
    int line_length = MIN(grid_row->n_significant, g->w);
    string_push(outbuffer, move(lineno, columnno));

    for (int col = 0; col < line_length; col++) {
      struct grid_cell *c = &grid_row->cells[col];
      apply_style(c->attr, c->fg, c->bg, outbuffer);
      string_push_slice(outbuffer, (char *)c->symbol.utf8, c->symbol.len);
    }

    int num_blanks = g->w - line_length;
    if (num_blanks > 0) {
      apply_style(0, color_default, color_default, outbuffer);
      string_memset(outbuffer, ' ', num_blanks);
    }
  }
}

void pane_draw_border(struct pane *p, struct string *b) {
  bool topmost = p->rect.window.y == 0;
  bool leftmost = p->rect.window.x == 0;

  char *corner = NULL;
  char *rightcorner = "─";
  if (topmost && leftmost) {
    corner = "╭";
  } else if (topmost) {
    corner = "┬";
  } else if (leftmost) {
    corner = "├";
  } else {
    corner = "├";
  }

  // char *bottomleftcorner = "\n\b├";
  char *disable_line_wrapping = "\x1b[?7l";
  char *enable_line_wrapping = "\x1b[?7h";
  char *bottomleftcorner = "\n\b│";
  char *pipe = "\n\b│";
  char *dash = "─";
  char *beforetitle = "┤";
  char *aftertitle = "├";
  int left = p->rect.window.x + 1;
  int top = p->rect.window.y + 1;
  int bottom = p->rect.window.h + top;
  int right = p->rect.window.w + left;

  string_push(b, disable_line_wrapping);
  // top left corner
  string_push(b, move(top, left));
  string_push(b, corner);
  // top line
  {
    int i = left + 1;
    int n = strlen(p->process);
    i += n + 3;
    string_push(b, dash);
    string_push(b, beforetitle);
    string_push_slice(b, p->process, n);
    string_push(b, aftertitle);
    for (; i < right - 1; i++) string_push(b, dash);
    string_push(b, rightcorner);
  }
  string_push(b, move(top, left + 1));
  for (int row = top + 1; row < bottom - 1; row++) {
    string_push(b, pipe);
  }
  string_push(b, bottomleftcorner);
  string_push(b, disable_line_wrapping);
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
  pane->fsm.w = pane->rect.client.w;
  pane->fsm.h = pane->rect.client.h;
  pane->fsm.on_report_cursor_position = on_report_mouse_position;
  pane->fsm.context = pane;
  if (pane->logfile > 0) write(pane->logfile, buf, n);
  fsm_process(&pane->fsm, buf, n);
}

void pane_resize(struct pane *pane, struct bounds outer) {
  // Refuse to go below a minimum size
  if (outer.w < 2) outer.w = 2;
  if (outer.h < 2) outer.h = 2;

  struct bounds inner = (struct bounds){.x = outer.x + pane->border,
                                        .y = outer.y + pane->border,
                                        .w = outer.w - pane->border,
                                        .h = outer.h - pane->border};
  if (pane->rect.window.w != outer.w || pane->rect.window.h != outer.h) {
    struct winsize ws = {.ws_col = inner.w, .ws_row = inner.h};
    if (pane->pty) ioctl(pane->pty, TIOCSWINSZ, &ws);
    if (pane->pid) kill(pane->pid, SIGWINCH);
  }

  // If anything changed about the window position / dimensions, do a full redraw
  if (pane->rect.window.w != outer.w || pane->rect.window.h != outer.h || pane->rect.window.x != outer.x ||
      pane->rect.window.y != outer.y) {
    grid_invalidate(pane->fsm.active_grid);
  }
  pane->rect.window = outer;
  pane->rect.client = inner;
}

void pane_start(struct pane *pane) {
  struct winsize panesize = {.ws_col = pane->rect.client.w, .ws_row = pane->rect.client.h};
  pid_t pid = forkpty(&pane->pty, NULL, NULL, &panesize);
  if (pid < 0) die("forkpty:");

  if (pid == 0) {
    char *argv[] = {"sh", "-c", pane->process, NULL};
    execvp("sh", argv);
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
}

int pane_count(struct pane *pane) {
  int n = 0;
  for (; pane; pane = pane->next) n++;
  return n;
}
