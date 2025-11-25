#include "platform.h"
#include <ctype.h>
#include <vte_host.h>
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
#include "vte.h"
#include "utils.h"
#include "virtual_terminal_sequences.h"

extern pid_t forkpty(int *, char *, struct termios *, struct winsize *);
struct vte_host *clients = NULL;
struct vte_host *focused = NULL;

void vte_host_destroy(struct vte_host *vte_host) {
  if (vte_host->pty > 0) {
    close(vte_host->pty);
    vte_host->pty = 0;
  }
  if (vte_host->pid > 0) {
    int status;
    kill(vte_host->pid, SIGTERM);
    pid_t result = waitpid(vte_host->pid, &status, WNOHANG);
    if (result == -1) die("waitpid:");
  }
  vte_destroy(&vte_host->vte);
  free(vte_host->cmdline);
  vte_host->pty = vte_host->pid = 0;
  vte_host->cmdline = nullptr;
}

struct vte_host *vte_host_from_pty(struct vte_host *p, int pty) {
  for (; p; p = p->next)
    if (p->pty == pty) return p;
  return nullptr;
}

struct vte_host *vte_host_from_pid(struct vte_host *p, int pid) {
  for (; p; p = p->next)
    if (p->pid == pid) return p;
  return nullptr;
}

struct sgr_param {
  uint8_t primary;
  uint8_t sub[4];
  int n_sub;
};

// Terminal emulators may not have infinite sapce set aside for SGR sequences,
// so let's split our sequences at this threshold. Such sequences should be unusual anyway.
#define MAX_LOAD 10
#define SGR_PARAMS_MAX 12
struct sgr_buffer {
  struct sgr_param params[SGR_PARAMS_MAX];
  uint8_t n;
};

// Concatenate the strings in `strings` into `dst`.
static void cat_strings(char *dst, int n, char **strings) {
  int i = 0;
  for (; strings && *strings; strings++) {
    const uint8_t *ch = (uint8_t *)*strings;
    for (; ch && *ch && i < n; ch++) dst[i++] = iscntrl(*ch) ? '.' : *ch;
  }
  dst[i] = 0;
}

void vte_host_update_cwd(struct vte_host *p) {
  if (platform.get_cwd_from_pty) {
    char buf[256];
    if (platform.get_cwd_from_pty(p->pty, buf, sizeof(buf))) {
      if (strncmp(buf, p->cwd, MIN(sizeof(buf), sizeof(p->cwd)))) {
        p->border_dirty = true;
        strncpy(p->cwd, buf, MIN(sizeof(buf), sizeof(p->cwd)));
        cat_strings(p->title, sizeof(p->title) - 1, (char *[]){p->cmdline, " — ", p->cwd, NULL});
      }
    }
  } else if (!p->title[0]) {
    // fallback to using the process as title
    cat_strings(p->title, sizeof(p->title) - 1, (char *[]){p->cmdline, NULL});
  }
}

static inline void sgr_buffer_push(struct sgr_buffer *b, int n) {
  // TODO: Is this possible?
  assert(b->n < SGR_PARAMS_MAX);
  b->params[b->n] = (struct sgr_param){.primary = n};
  b->n++;
}
static inline void sgr_buffer_add_param(struct sgr_buffer *b, int sub) {
  assert(b->n);
  int k = b->n - 1;
  struct sgr_param *p = &b->params[k];
  p->sub[p->n_sub] = sub;
  p->n_sub++;
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
      sgr_buffer_add_param(sgr, 5);
      sgr_buffer_add_param(sgr, col->table);
    }
  } else if (col->cmd == COLOR_RGB) {
    sgr_buffer_push(sgr, fg ? 38 : 48);
    sgr_buffer_add_param(sgr, 2);
    sgr_buffer_add_param(sgr, col->r);
    sgr_buffer_add_param(sgr, col->g);
    sgr_buffer_add_param(sgr, col->b);
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

  if (!color_equals(&fg, &style->fg)) {
    apply_color(&style->fg, true, &sgr);
  }
  if (!color_equals(&bg, &style->bg)) {
    apply_color(&style->bg, false, &sgr);
  }

  attr = style->attr;
  fg = style->fg;
  bg = style->bg;

  if (sgr.n) {
    string_push(outbuffer, u8"\x1b[");
    int current_load = 0;
    for (int i = 0; i < sgr.n; i++) {
      struct sgr_param *p = &sgr.params[i];
      int this_load = 1 + p->n_sub;
      if (current_load + this_load > MAX_LOAD) {
        outbuffer->content[outbuffer->len - 1] = 'm';
        string_push(outbuffer, u8"\x1b[");
        current_load = 0;
      }
      current_load += this_load;
      string_push_int(outbuffer, p->primary);
      for (int j = 0; j < p->n_sub; j++) {
        // I would prefer to properly use ':' to split subparameters here, but it appears that 
        // some terminal emulators do not properly implement this. For compatibility, 
        // use ';' as a separator instead.
        string_push_char(outbuffer, ';');
        string_push_int(outbuffer, p->sub[j]);
      }
      string_push_char(outbuffer, ';');
    }
    outbuffer->content[outbuffer->len - 1] = 'm';
  }
}

void vte_host_draw(struct vte_host *vte_host, bool redraw, struct string *outbuffer) {
  // Ensure the grid content is in sync with the vte_host just-in-time
  vte_set_size(&vte_host->vte, vte_host->rect.client.w, vte_host->rect.client.h);

  struct grid *g = vte_get_current_grid(&vte_host->vte);
  for (int row = 0; row < g->h; row++) {
    struct grid_row *grid_row = &g->rows[row];
    if (!redraw && !grid_row->dirty) continue;
    grid_row->dirty = false;
    int columnno = 1 + vte_host->rect.client.x;
    int lineno = 1 + vte_host->rect.client.y + row;
    int line_length = MIN(grid_row->eol, g->w);
    string_push_csi(outbuffer, INT_SLICE(lineno, columnno), "H");

    for (int col = 0; col < line_length; col++) {
      struct grid_cell *c = &grid_row->cells[col];
      apply_style(&c->style, outbuffer);
      uint8_t n = 0;
      for (; n < 4 && c->symbol.utf8[n]; n++) string_push_char(outbuffer, c->symbol.utf8[n]);
    }

    int num_blanks = g->w - line_length;
    if (num_blanks > 4) { /* CSI Pn X -- Erase Pn characters after cursor */
      apply_style(&style_default, outbuffer);
      string_push_csi(outbuffer, INT_SLICE(num_blanks), "X");
    } else { /* Micro optimization. Insert spaces if the number of blanks is very small */
      apply_style(&style_default, outbuffer);
      string_memset(outbuffer, ' ', num_blanks);
    }
  }
}

// TODO: This sucks
// Alternative implementation: Walk vte_host list and termine where all the borders are
// Then draw the borders, and style the borders touching the focused vte_host
void vte_host_draw_border(struct vte_host *p, struct string *b, bool focused) {
  if (p->border_width == 0 || !p->border_dirty) return;
  static const struct grid_cell_style focused_style = {.attr = ATTR_BOLD, .fg = {.cmd = COLOR_TABLE, .table = 9}};
  static const struct grid_cell_style normal_style = {.attr = 0, .fg = {.cmd = COLOR_TABLE, .table = 4}};
  p->border_dirty = false;
  bool topmost = p->rect.window.y == 0;
  bool leftmost = p->rect.window.x == 0;
  bool has_right_neighbor = p->rect.window.x + p->rect.window.w < ws_current.ws_col;

  uint8_t *topleft_corner = NULL;
  uint8_t *topright_corner = u8"─";
  if (topmost && leftmost) {
    // corner = "╭";
    topleft_corner = u8"─";
    if (p->rect.window.w < ws_current.ws_col) topright_corner = u8"┬";
  } else if (topmost) {
    topleft_corner = u8"┬";
  } else if (leftmost) {
    topleft_corner = u8"─";
    topright_corner = u8"┤";
  } else {
    topleft_corner = u8"├";
  }
  if (!leftmost && !topmost) {
    topleft_corner = u8"│";
  }

  if (!topmost) {
    bool before = true;
    for (struct vte_host *c = clients; c; c = c->next) {
      if (c == p) {
        before = false;
        continue;
      }
      if (c->rect.window.y == p->rect.window.y) {
        if (before) {
          topleft_corner = u8"┼";
        } else {
          topright_corner = u8"┼";
        }
        break;
      }
    }
  }

  // char *bottomleftcorner = "\n\b├";
  uint8_t *bottomleftcorner = u8"\n\b│";
  uint8_t *pipe = u8"\n\b│";
  uint8_t *dash = u8"─";
  uint8_t *beforetitle = u8" "; //"┤";
  uint8_t *aftertitle = u8" ";  //"├";
  int left = p->rect.window.x + 1;
  int top = p->rect.window.y + 1;
  int bottom = p->rect.window.h + top;
  int right = p->rect.window.w + left;

  apply_style(focused ? &focused_style : &normal_style, b);

  // top left corner
  string_push_csi(b, INT_SLICE(top, left), "H");
  string_push(b, topleft_corner);
  // top line
  {
    int i = left + 1;
    // TODO: Technically process can contain utf8 which could be problematic with strlen
    int n = utf8_strlen(p->title);
    i += n + 3;
    string_push(b, dash);
    string_push(b, beforetitle);
    string_push(b, (uint8_t *)p->title);
    string_push(b, aftertitle);
    for (; i < right; i++) string_push(b, dash);
    string_push(b, topright_corner);
  }
  if (!leftmost) {
    string_push_csi(b, INT_SLICE(top, left + 1), "H");
    for (int row = top + 1; row < bottom - 1; row++) {
      string_push(b, pipe);
    }
    string_push(b, bottomleftcorner);
  }
  if (has_right_neighbor) {
    string_push_csi(b, INT_SLICE(top, right + 1), "H");
    for (int row = top + 1; row < bottom; row++) {
      string_push(b, pipe);
    }
  }
  apply_style(&style_default, b);
}

void vte_host_process_output(struct vte_host *vte_host, uint8_t *buf, int n) {
  // Pass current size information to vte so it can determine if grids should be resized
  vte_set_size(&vte_host->vte, vte_host->rect.client.w, vte_host->rect.client.h);
  vte_process(&vte_host->vte, buf, n);
  string_flush(&vte_host->vte.pending_output, vte_host->pty, nullptr);
}

static inline bool bounds_equal(const struct bounds *const a, const struct bounds *const b) {
  return a->x == b->x && a->y == b->y && a->w == b->w && a->h == b->h;
}

void vte_host_resize(struct vte_host *vte_host, struct bounds outer) {
  // Refuse to go below a minimum size
  if (outer.w < 2) outer.w = 2;
  if (outer.h < 2) outer.h = 2;

  bool leftmost = outer.x == 0;

  struct bounds inner = (struct bounds){.x = outer.x + (leftmost ? 0 : vte_host->border_width),
                                        .y = outer.y + vte_host->border_width,
                                        .w = outer.w - (leftmost ? 0 : vte_host->border_width),
                                        .h = outer.h - vte_host->border_width};
  if (vte_host->rect.window.w != outer.w || vte_host->rect.window.h != outer.h) {
    struct winsize ws = {.ws_col = inner.w, .ws_row = inner.h};
    if (vte_host->pty) ioctl(vte_host->pty, TIOCSWINSZ, &ws);
    if (vte_host->pid) kill(vte_host->pid, SIGWINCH);
    if (vte_host->border_width) vte_host->border_dirty = true;
  }

  // If anything changed about the window position / dimensions, do a full redraw
  if (!bounds_equal(&outer, &vte_host->rect.window) || !bounds_equal(&inner, &vte_host->rect.client)) {
    struct grid *g = vte_get_current_grid(&vte_host->vte);
    grid_invalidate(g);
    vte_host->border_dirty = true;
  }
  vte_host->rect.window = outer;
  vte_host->rect.client = inner;
}

void vte_host_start(struct vte_host *vte_host) {
  struct winsize vte_hostsize = {.ws_col = vte_host->rect.client.w, .ws_row = vte_host->rect.client.h};
  pid_t pid = forkpty(&vte_host->pty, NULL, NULL, &vte_hostsize);
  if (pid < 0) die("forkpty:");

  if (pid == 0) {
    char *argv[] = {"sh", "-c", vte_host->cmdline, NULL};
    execvp("sh", argv);
    die("execlp:");
  }
  vte_host->pid = pid;
  set_nonblocking(vte_host->pty);
}

void vte_host_remove(struct vte_host **lst, struct vte_host *rem) {
  assert(lst);
  assert(*lst);
  assert(rem);
  if (*lst == rem) {
    *lst = rem->next;
    return;
  }
  struct vte_host *prev = *lst;
  for (struct vte_host *p = prev->next; p; p = p->next) {
    if (p == rem) {
      prev->next = p->next;
    }
    prev = p;
  }
}

void vte_host_notify_focus(struct vte_host *p, bool focused) {
  if (p) {
    p->border_dirty = true;
    if (p->pty && p->vte.options.focus_reporting) {
      if (focused) {
        write(p->pty, vt_focus_in.content, vt_focus_in.len);
      } else {
        write(p->pty, vt_focus_out.content, vt_focus_out.len);
      }
    }
  }
}

int vte_host_count(struct vte_host *vte_host) {
  int n = 0;
  for (; vte_host; vte_host = vte_host->next) n++;
  return n;
}
