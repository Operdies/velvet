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

extern pid_t forkpty(int *, char *, struct termios *, struct winsize *);
struct pane *clients = NULL;
struct pane *focused = NULL;

// sprintf is extremely slow because it needs to deal with format strings. We can do better
// when we know we just want to write a small positive integer
static inline int write_int(char *dst, int n) {
  assert(n >= 0);
  const int max = 11;
  char buf[max];
  int idx = max;

  do {
    buf[--idx] = '0' + n % 10;
    n /= 10;
  } while (n);

  for (int i = idx; i < max; i++, dst++) *dst = buf[i];
  return max - idx;
#undef INT_MAX_CHAR
}

static const char *move(int row, int col) {
  static char buf[20] = "\x1b[";
  int i_buf = 2;
  i_buf += write_int(buf + i_buf, row);
  buf[i_buf++] = ';';
  i_buf += write_int(buf + i_buf, col);
  buf[i_buf++] = 'H';
  buf[i_buf++] = 0;
  logmsg("Move: %s", buf + 1);
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

struct sgr_buffer {
  int params[100];
  int n;
};

static inline void sgr_buffer_push(struct sgr_buffer *b, int n) {
  // TODO: Is this possible?
  assert(b->n < 100);
  b->params[b->n] = n;
  b->n++;
}

static void apply_color(const struct color *const col, bool fg, struct sgr_buffer *sgr) {
  if (col->cmd == COLOR_RESET) {
    sgr_buffer_push(sgr, fg ? 39 : 49);
  } else if (col->cmd == COLOR_TABLE) {
    if (col->table <= 7) {
      sgr_buffer_push(sgr, (fg ? 30 : 40) + col->table);
    } else if (col->table <= 15) {
      sgr_buffer_push(sgr, (fg ? 90 : 100) + col->table - 8);
    } else {
      sgr_buffer_push(sgr, fg ? 38 : 48);
      sgr_buffer_push(sgr, 5);
      sgr_buffer_push(sgr, col->table);
    }
  } else if (col->cmd == COLOR_RGB) {
    sgr_buffer_push(sgr, fg ? 38 : 48);
    sgr_buffer_push(sgr, 2);
    sgr_buffer_push(sgr, col->r);
    sgr_buffer_push(sgr, col->g);
    sgr_buffer_push(sgr, col->b);
  }
}

/* Write the specified style attributes to the outbuffer, but only if they are different from what is currently stored.
 * Note that the use of statics means that this function is not thread safe or re-entrant. It should only be used for
 * streaming content to the screen. */
static inline void apply_style(const struct grid_cell_style *const style, struct string *outbuffer) {
  static struct color fg = color_default;
  static struct color bg = color_default;
  struct sgr_buffer sgr = {.n = 0};

  static uint32_t attr;
  // 1. Handle attributes
  if (attr != style->attr) {
    if (style->attr == 0) {
      // Unfortunately this also resets colors, so we need to set those again
      // Technically we could track what styles are active here and disable those specifically,
      // but it is much simpler to just eat the color reset.
      fg = bg = color_default;
      sgr_buffer_push(&sgr, 0);
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
        uint32_t should_be_on = features[i] & style->attr;
        if (is_on && !should_be_on) {
          if (i == 1) {
            // annoying special case for bold
            sgr_buffer_push(&sgr, 22);
          } else {
            sgr_buffer_push(&sgr, 20 + i);
          }
        } else if (!is_on && should_be_on) {
          sgr_buffer_push(&sgr, i);
        }
      }
    }
  }

  if (memcmp(&fg, &style->fg, sizeof(fg)) != 0) {
    apply_color(&style->fg, true, &sgr);
  }
  if (memcmp(&bg, &style->bg, sizeof(bg)) != 0) {
    apply_color(&style->bg, false, &sgr);
  }

  attr = style->attr;
  fg = style->fg;
  bg = style->bg;

  if (sgr.n) {
    string_push(outbuffer, "\x1b[");
    for (int i = 0; i < sgr.n; i++) {
      string_push_int(outbuffer, sgr.params[i]);
      string_push_char(outbuffer, ';');
    }
    outbuffer->content[outbuffer->len - 1] = 'm';
  }
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
      apply_style(&c->style, outbuffer);
      string_push_slice(outbuffer, (char *)c->symbol.utf8, c->symbol.len);
    }

    int num_blanks = g->w - line_length;
    if (num_blanks > 0) {
      apply_style(&style_default, outbuffer);
      string_memset(outbuffer, ' ', num_blanks);
    }
  }
}

// TODO: This sucks
// Alternative implementation: Walk pane list and termine where all the borders are 
// Then draw the borders, and style the borders touching the focused pane
void pane_draw_border(struct pane *p, struct string *b) {
  if (p->border_width == 0 || !p->border_dirty) return;
  static const struct grid_cell_style focused_style = {.attr = ATTR_BOLD, .fg = {.cmd = COLOR_TABLE, .table = 9}};
  static const struct grid_cell_style normal_style = {.attr = 0, .fg = {.cmd = COLOR_TABLE, .table = 4}};
  p->border_dirty = false;
  bool topmost = p->rect.window.y == 0;
  bool leftmost = p->rect.window.x == 0;
  bool has_right_neighbor = p->rect.window.x + p->rect.window.w < ws_current.ws_col;

  char *topleft_corner = NULL;
  char *topright_corner = "─";
  if (topmost && leftmost) {
    // corner = "╭";
    topleft_corner = "─";
    if (p->rect.window.w < ws_current.ws_col)
      topright_corner = "┬";
  } else if (topmost) {
    topleft_corner = "┬";
  } else if (leftmost) {
    topleft_corner = "─";
    topright_corner = "┤";
  } else {
    topleft_corner = "├";
  }
  if (!leftmost && !topmost) {
    topleft_corner = "│";
  }

  if (!topmost)
  {
    bool before = true;
    for (struct pane *c = clients; c; c = c->next) {
      if (c == p) {
        before = false;
        continue;
      }
      if (c->rect.window.y == p->rect.window.y) {
        if (before) {
          topleft_corner = "┼";
        } else {
          topright_corner = "┼";
        }
        break;
      }
    }
  }

  // char *bottomleftcorner = "\n\b├";
  char *bottomleftcorner = "\n\b│";
  char *pipe = "\n\b│";
  char *dash = "─";
  char *beforetitle = " ";//"┤";
  char *aftertitle = " ";//"├";
  int left = p->rect.window.x + 1;
  int top = p->rect.window.y + 1;
  int bottom = p->rect.window.h + top;
  int right = p->rect.window.w + left;

  apply_style(p->has_focus ? &focused_style : &normal_style, b);

  // top left corner
  string_push(b, move(top, left));
  string_push(b, topleft_corner);
  // top line
  {
    int i = left + 1;
    // TODO: Technically process can contain utf8 which could be problematic with strlen
    int n = strlen(p->process);
    i += n + 3;
    string_push(b, dash);
    string_push(b, beforetitle);
    string_push_slice(b, p->process, n);
    string_push(b, aftertitle);
    for (; i < right; i++) string_push(b, dash);
    string_push(b, topright_corner);
  }
  if (!leftmost) {
    string_push(b, move(top, left + 1));
    for (int row = top + 1; row < bottom - 1; row++) {
      string_push(b, pipe);
    }
    string_push(b, bottomleftcorner);
  }
  if (has_right_neighbor) {
    string_push(b, move(top, right + 1));
    for (int row = top + 1; row < bottom; row++) {
      string_push(b, pipe);
    }
  }
  apply_style(&style_default, b);
}

static void on_report_mouse_position(void *context, int row, int col) {
  struct pane *p = context;
  if (p->pty) {
    char buf[30] = "\x1b[";
    int n = 2;
    n = write_int(buf + n, row);
    buf[++n] = ';';
    n += write_int(buf + n, col);
    buf[++n] = 'R';
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

  bool leftmost = outer.x == 0;

  struct bounds inner = (struct bounds){.x = outer.x + (leftmost ? 0 : pane->border_width),
                                        .y = outer.y + pane->border_width,
                                        .w = outer.w - (leftmost ? 0 : pane->border_width),
                                        .h = outer.h - pane->border_width};
  if (pane->rect.window.w != outer.w || pane->rect.window.h != outer.h) {
    struct winsize ws = {.ws_col = inner.w, .ws_row = inner.h};
    if (pane->pty) ioctl(pane->pty, TIOCSWINSZ, &ws);
    if (pane->pid) kill(pane->pid, SIGWINCH);
    if (pane->border_width) pane->border_dirty = true;
  }

  // If anything changed about the window position / dimensions, do a full redraw
  if (pane->rect.window.w != outer.w || pane->rect.window.h != outer.h || pane->rect.window.x != outer.x ||
      pane->rect.window.y != outer.y) {
    grid_invalidate(pane->fsm.active_grid);
    pane->border_dirty = true;
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
