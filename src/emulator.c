#include "emulator.h"
#include "collections.h"
#include "utils.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NUL 0
#define BELL '\a'
#define BSP '\b'
#define DEL 0x7f
#define ESC 0x1b
#define FORMFEED '\f'
#define NEWLINE '\n'
#define RET '\r'
#define TAB '\t'
#define VTAB '\v'
#define CSI '['
#define DCS 'P'
#define OSC ']'

#define debugthis logmsg("Function: %s, File: %s, Line: %d\n", __func__, __FILE__, __LINE__)

static void grid_insert(struct grid *g, struct cell c, bool wrap);
static void grid_destroy(struct grid *grid);
static void grid_move_cursor(struct grid *g, int x, int y);

static void send_escape(struct fsm *f) {
  assert(f->seq.n);
  logmsg("Escape: %.*s", f->seq.n - 1, f->seq.buffer + 1);
  fwrite(f->seq.buffer, 1, f->seq.n, stdout);
  f->seq.n = 0;
}

// static void parse_csi_params(uint8_t *buffer, size_t start, size_t end, int *params, int *count) {
//   *count = 0;
//   size_t i = start;
//   while (i < end) {
//     int val = 0;
//     int digits = 0;
//     while (i < end && buffer[i] >= '0' && buffer[i] <= '9') {
//       val = val * 10 + (buffer[i] - '0');
//       i++;
//       digits++;
//     }
//     if (digits == 0) val = 0;
//     params[(*count)++] = val;
//     if (i < end && buffer[i] == ';') i++;
//   }
// }

static void parse_csi_params(uint8_t *buffer, int len, int *params, int nparams, int *count) {
  *count = 0;
  int i = 0;
  for (; i < len && *count < nparams;) {
    int val = 0;
    for (; i < len && buffer[i] >= '0' && buffer[i] <= '9'; i++) {
      uint8_t digit = buffer[i];
      val = (val * 10) + (digit - '0');
    }
    params[*count] = val;
    *count = *count + 1;
    for (; i < len && buffer[i] != ';'; i++);
    i++;
  }
}

static void grid_erase_line_range(struct grid *g, struct cursor start, struct cursor end) {
  assert(start.y == end.y);
  assert(start.x <= end.x);
  struct cursor *c = &g->cursor;
  assert(c->y < g->h);
  assert(c->y >= 0);
  g->dirty[c->y] = true;
  for (int i = start.x; i < end.x && i < g->w; i++) {
    int idx = c->y * g->w + i;
    g->cells[idx] = empty_cell;
  }
}

static void apply_buffered_csi(struct fsm *fsm) {
  uint8_t *buffer = fsm->seq.buffer;
  size_t len = fsm->seq.n;
  uint8_t final_byte = buffer[len - 1];
  struct grid *g = fsm->active_grid;

  if (len < 3) {
    logmsg("CSI sequence shorter than 3 bytes!!");
    return;
  }

  logmsg("CSI: %.*s", fsm->seq.n - 1, fsm->seq.buffer + 1);

  int params[10] = {0};
  int count = 0;
  parse_csi_params(buffer + 2, len - 3, params, 10, &count);
  int move = count > 0 ? params[0] : 1;

  switch (final_byte) {
  case 'K': {
    int mode = params[0];
    struct cursor start = fsm->active_grid->cursor;
    struct cursor end = fsm->active_grid->cursor;
    if (mode == 0) {
      // erase from cursor to end
      end.x = g->w;
    } else if (mode == 1) {
      // erase from start to cursor
      start.x = 0;
    } else if (mode == 2) {
      // erase entire line
      start.x = 0;
      end.x = g->w;
    } else {
      // unknown: erase from cursor to end
      end.x = g->w;
    }
    grid_erase_line_range(g, start, end);
  } break;
  case 'A': { /* move up */
    grid_move_cursor(g, 0, -move);
  } break;
  case 'B': { /* move down */
    grid_move_cursor(g, 0, move);
  } break;
  case 'C': { /* move right */
    grid_move_cursor(g, move, 0);
  } break;
  case 'D': { /* move left */
    grid_move_cursor(g, -move, 0);
  } break;
  default: {
    // logmsg("Unhandled CSI: %.*s", fsm->seq.n - 1, fsm->seq.buffer + 1);
  } break;
  }
}

static void process_csi(struct fsm *fsm, unsigned char ch) {
  fsm->seq.buffer[fsm->seq.n++] = ch;
  if (ch >= 0x40 && ch <= 0x7E) {
    apply_buffered_csi(fsm);
    fsm->state = fsm_ground;
  } else if (fsm->seq.n >= MAX_ESC_SEQ_LEN) {
    fsm->state = fsm_ground;
  }
}

static void fix_grid(struct grid *g, int w, int h) {
  if (!g->cells) {
    g->cells = calloc(w * h, sizeof(*g->cells));
    g->dirty = calloc(w * h, sizeof(*g->dirty));
    g->h = h;
    g->w = w;

    for (int i = 0; i < h; i++) {
      g->dirty[i] = true;
    }
  }

  // TODO: Enlarge grids if too small
  // TODO: Reflow grids if dimensions changed
  if (g->h != h || g->w != w) {
    struct grid new = {.w = w, .h = h};
    fix_grid(&new, w, h);
    for (int i = 0; i < g->w; i++) {
      for (int j = 0; j < g->h; j++) {
        int index = j + ((i + g->offset) % g->h);
        struct cell *c = &g->cells[index];
        grid_insert(&new, *c, true);
      }
    }
    // TODO: Iterate all cells from `g' and insert them in `new'
    // Insertion function should reflow
    grid_destroy(g);
    *g = new;
  }
}

static struct grid *fsm_active_grid2(struct fsm *fsm) {
  return fsm->opts.alternate_screen ? &fsm->alternate : &fsm->primary;
}

static void fix_grids(struct fsm *fsm) {
  fix_grid(&fsm->primary, fsm->w, fsm->h);
  fix_grid(&fsm->alternate, fsm->w, fsm->h);
  fsm->active_grid = fsm_active_grid2(fsm);
}

static void grid_clear_line(struct grid *g, int line) {
  g->dirty[line] = true;
  for (int i = 0; i < g->w; i++) {
    g->cells[line * g->w + i] = empty_cell;
  }
}

static inline int grid_get_logical_line(struct grid *g) {
  int physical = g->cursor.y;
  assert(physical >= 0 && physical < g->h);
  int logical = (g->h + physical - g->offset) % g->h;
  assert(logical >= 0 && logical < g->h);
  return logical;
}
static inline void grid_set_logical_line(struct grid *g, int logical) {
  assert(logical >= 0 && logical < g->h);
  int physical = (g->h + logical + g->offset) % g->h;
  assert(physical >= 0 && physical < g->h);
  g->cursor.y = physical;
}

static void grid_move_cursor(struct grid *g, int x, int y) {
  // For the 'x' coordinate, the logical and physical coordinates are always synced
  g->cursor.x = CLAMP(g->cursor.x + x, 0, g->w - 1);

  // This is a bit more convoluted because we need to translate physical / logical coordinates
  int ly = grid_get_logical_line(g);
  ly = CLAMP(ly + y, 0, g->h - 1);
  grid_set_logical_line(g, ly);
}

static void grid_advance_cursor_y(struct grid *g) {
  struct cursor *c = &g->cursor;
  c->y = (c->y + 1) % g->h;
  if (c->y == g->offset) {
    grid_clear_line(g, g->offset);
    g->offset = (g->offset + 1) % g->h;
    for (int i = 0; i < g->h; i++) {
      g->dirty[i] = true;
    }
  }
  assert(c->y < g->h);
}
static void grid_advance_cursor(struct grid *g, bool wrap) {
  struct cursor *c = &g->cursor;
  c->x++;
  if (c->x >= g->w) {
    if (wrap) {
      c->x = 0;
      grid_advance_cursor_y(g);
    } else {
      c->x = g->w - 1;
    }
  }
}

static void grid_insert(struct grid *g, struct cell c, bool wrap) {
  /* Implementation notes:
   * 1. The width of a cell depends on the content. Some characters are double width. For now, we assume all characters
   * are single width.
   * */
  // TODO: Handle y coordinate wrapping
  // TODO: line offset??
  struct cursor *cur = &g->cursor;
  // logmsg("[%dx%d] Insert line: %d, offset: %d", g->w, g->h, cur->y, g->offset);
  g->dirty[cur->y] = true;
  g->cells[cur->y * g->w + cur->x] = c;
  grid_advance_cursor(g, wrap);
}

static void ground_esc(struct fsm *fsm, uint8_t ch) {
  fsm->state = fsm_escape;
  fsm->seq.n = 0;
  fsm->seq.buffer[fsm->seq.n++] = ch;
}
static void ground_noop(struct fsm *fsm, uint8_t ch) {
}

static void grid_carriage_return(struct grid *g) {
  g->cursor.x = 0;
}

static void ground_carriage_return(struct fsm *fsm, uint8_t ch) {
  grid_carriage_return(fsm->active_grid);
}

static void grid_backspace(struct grid *g) {
  // Move cursor back, but not past the first column
  g->cursor.x = MAX(0, g->cursor.x - 1);
}

static void ground_backspace(struct fsm *fsm, uint8_t ch) {
  grid_backspace(fsm->active_grid);
}
static void ground_vtab(struct fsm *fsm, uint8_t ch) {
  // Ignore wrapping rules and preserve column
  debugthis;
  grid_advance_cursor_y(fsm->active_grid);
}
static void ground_tab(struct fsm *fsm, uint8_t ch) {
  debugthis;
  const int tabwidth = 8;
  int x = fsm->active_grid->cursor.x;
  int x2 = ((x / tabwidth) + 1) * tabwidth;
  int numSpaces = x2 - x;
  struct cell c = fsm->cell;
  c.symbol = utf8_blank;
  for (int i = 0; i < numSpaces; i++) {
    grid_insert(fsm->active_grid, c, false);
  }
}

static void ground_bell(struct fsm *fsm, uint8_t ch) {
  debugthis;
  write(STDOUT_FILENO, "\a", 1);
}

static void ground_newline(struct fsm *fsm, uint8_t ch) {
  debugthis;
  grid_advance_cursor_y(fsm->active_grid);
  if (fsm->opts.auto_return) grid_carriage_return(fsm->active_grid);
}

static void ground_accept(struct fsm *fsm) {
  struct utf8 clear = {0};
  struct grid *g = fsm->active_grid;
  grid_insert(g, fsm->cell, !fsm->opts.nowrap);
  fsm->cell.symbol = clear;
}
static void ground_reject(struct fsm *fsm) {
  debugthis;
  struct utf8 clear = {0};
  struct utf8 copy = fsm->cell.symbol;
  fsm->cell.symbol = clear;
  // If we are rejecting this symbol, we should
  // Render a replacement char for this sequence (U+FFFD)
  struct cell replacement = {.symbol = utf8_fffd};
  struct grid *g = fsm->active_grid;
  grid_insert(g, replacement, !fsm->opts.nowrap);
  if (copy.len > 1) fsm_process(fsm, &copy.utf8[1], copy.len - 1);
}

static inline uint8_t utf8_expected_length2(uint8_t ch) {
  if ((ch & 0x80) == 0x00)
    return 1; /* 0xxxxxxx */
  else if ((ch & 0xE0) == 0xC0)
    return 2; /* 110xxxxx */
  else if ((ch & 0xF0) == 0xE0)
    return 3; /* 1110xxxx */
  else if ((ch & 0xF8) == 0xF0)
    return 4; /* 11110xxx */
  else
    return 0; /* invalid leading byte or continuation byte */
}

void utf8_push(struct utf8 *u, uint8_t byte) {
  assert(u->len < 8);
  if (!u->len) {
    uint8_t expected_length = utf8_expected_length2(byte);
    u->expected = expected_length;
  }
  u->utf8[u->len] = byte;
  u->len++;
}

static void process_ground(struct fsm *fsm, uint8_t ch) {
  // These symbols have special behavior in terms of how they affect layout
  static void (*const dispatch[UINT8_MAX])(struct fsm *, uint8_t) = {
      [NUL] = ground_noop,        [ESC] = ground_esc,   [RET] = ground_carriage_return,
      [BSP] = ground_backspace,   [BELL] = ground_bell, [DEL] = ground_noop,
      [TAB] = ground_tab,         [VTAB] = ground_vtab, [FORMFEED] = ground_newline,
      [NEWLINE] = ground_newline,
  };

  if (dispatch[ch]) {
    dispatch[ch](fsm, ch);
    return;
  }

  utf8_push(&fsm->cell.symbol, ch);
  uint8_t len = fsm->cell.symbol.len;
  uint8_t exp = fsm->cell.symbol.expected;
  assert(exp >= 0), assert(exp <= 4);

  if (exp == 0) {
    // Invalid sequence
    ground_reject(fsm);
    return;
  }

  // continue processing
  if (len < exp) return;

  // If this is not a continuation byte, reject the sequence
  if (len > 1 && (ch & 0xC0) != 0x80) {
    ground_reject(fsm);
    return;
  }

  // If the sequence is too long, reject it
  if (len > 4) {
    ground_reject(fsm);
    return;
  }

  // Accept the sequence
  ground_accept(fsm);
}

void fsm_process(struct fsm *fsm, unsigned char *buf, int n) {
  fix_grids(fsm);
  for (int i = 0; i < n; i++) {
    uint8_t ch = buf[i];
    switch (fsm->state) {
    case fsm_ground: {
      process_ground(fsm, ch);
    } break;
    case fsm_escape: {
      switch (ch) {
      case CSI: {
        fsm->state = fsm_csi;
        fsm->seq.buffer[fsm->seq.n++] = ch;
      } break;
      case OSC: {
        fsm->state = fsm_osc;
        fsm->seq.buffer[fsm->seq.n++] = ch;
      } break;
      case DCS: {
        fsm->state = fsm_dcs;
        fsm->seq.buffer[fsm->seq.n++] = ch;
      } break;
        /* legacy save/restore */
      case '7': {
        fsm->state = fsm_ground;
        fsm->active_grid->saved_cursor = fsm->active_grid->cursor;
      } break;
      case '8': {
        fsm->state = fsm_ground;
        fsm->active_grid->cursor = fsm->active_grid->saved_cursor;
      } break;
      default: {
        // Unrecognized escape. Treat this char as escaped and continue parsing normally.
        fsm->state = fsm_ground;
        break;
      }
      }
    } break;
    case fsm_dcs: {
      fsm->seq.buffer[fsm->seq.n++] = ch;
      if (ch == '\\' && fsm->seq.buffer[fsm->seq.n] == ESC) {
        fsm->state = fsm_ground;
      } else if (fsm->seq.n >= MAX_ESC_SEQ_LEN) {
        fsm->state = fsm_ground;
      }
    } break;
    case fsm_osc: {
      fsm->seq.buffer[fsm->seq.n++] = ch;
      if (ch == BELL) {
        send_escape(fsm);
        fsm->state = fsm_ground;
      } else if (fsm->seq.n >= MAX_ESC_SEQ_LEN) {
        fsm->state = fsm_ground;
      }
    } break;
    case fsm_csi: {
      process_csi(fsm, ch);
    } break;
    default: {
      assert(!"Unreachable");
    }
    }
  }
}

void grid_destroy(struct grid *grid) {
  free(grid->cells);
  free(grid->dirty);
  grid->cells = NULL;
  grid->dirty = NULL;
}

void fsm_destroy(struct fsm *fsm) {
  grid_destroy(&fsm->primary);
  grid_destroy(&fsm->alternate);
}
