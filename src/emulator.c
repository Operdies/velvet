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
#define ESC '\e'
#define FORMFEED '\f'
#define NEWLINE '\n'
#define RET '\r'
#define TAB '\t'
#define VTAB '\v'
#define CSI '['
#define DCS 'P'
#define OSC ']'

#define debugthis logmsg("Function: %s, File: %s, Line: %d\n", __func__, __FILE__, __LINE__)

#define grid_start(g) (0)
#define grid_end(g) (g->w - 1)
#define grid_top(g) (0)
#define grid_bottom(g) (g->h - 1)
#define grid_column(g) (g->cursor.x)
#define grid_line(g) (g->cursor.y)

static void grid_insert(struct grid *g, struct cell c, bool wrap);
static void grid_destroy(struct grid *grid);
static void grid_move_cursor(struct grid *g, int x, int y);
static void grid_set_visual_cursor(struct grid *g, int x, int y);
static inline struct cell *grid_current_cell(struct grid *g);
static void grid_newline(struct grid *g, bool carriage);
static void grid_advance_cursor_y_reverse(struct grid *g);
static void grid_advance_cursor_y(struct grid *g);
struct cell *grid_current_line(struct grid *g);

static void send_escape(struct fsm *f) {
  assert(f->seq.n);
  logmsg("Escape: %.*s", f->seq.n - 1, f->seq.buffer + 1);
  fwrite(f->seq.buffer, 1, f->seq.n, stdout);
  f->seq.n = 0;
}

static void fsm_update_active_grid(struct fsm *fsm) {
  struct grid *g = fsm->opts.alternate_screen ? &fsm->alternate : &fsm->primary;
  if (fsm->active_grid != g) {
    logmsg("Activate %s screen", fsm->opts.alternate_screen ? "secondary" : "primary");
    fsm->active_grid = g;
    for (int i = 0; i < g->h; i++) g->dirty[i] = true;
  }
}

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

/* inclusive erase between two cursor positions */
static void grid_erase_between_cursors(struct grid *g, struct cursor from, struct cursor to) {
  struct cursor *c = &g->cursor;
  assert(c->y < g->h);
  assert(c->y >= 0);
  for (int y = from.y; y <= to.y; y++) {
    g->dirty[c->y] = true;
    struct cell *line = &g->cells[y * g->w];
    int x = y == from.y ? from.x : 0;
    int end = y == to.y ? to.x : grid_end(g);

    // Track significant (deliberately inserted) characters. Needed for reflowing.
    if (line->n_significant <= end) {
      // If we are deleting the last significant cell, then we need to rewind the counter
      line->n_significant = MAX(line->n_significant, x);
    }
    for (; x <= end; x++) {
      line[x] = empty_cell;
    }
  }
}

static void grid_insert_blanks_at_cursor(struct grid *g, int n) {
  struct cell *line = &g->cells[g->w * grid_line(g)];
  int lcol = grid_column(g);
  // ]1@ transforms:
  // |some line_ here|
  // |some line_  her|
  for (int col = grid_end(g); col >= lcol; col--) {
    int rcol = col - n;
    struct cell replacement = rcol < lcol ? empty_cell : line[rcol];
    line[col] = replacement;
  }
}

static void grid_shift_from_cursor(struct grid *g, int n) {
  if (n == 0) return;
  struct cell *line = &g->cells[g->w * grid_line(g)];
  g->dirty[grid_line(g)] = true;
  for (int col = grid_column(g); col < grid_end(g); col++) {
    int rcol = col + n;
    struct cell replacement = rcol > grid_end(g) ? empty_cell : line[rcol];
    line[col] = replacement;
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
  int dec = buffer[2] == '?'; /* DEC Private Mode indicator */
  parse_csi_params(buffer + (2 + dec), len - (3 + dec), params, 10, &count);
  int param1 = count > 0 ? params[0] : 1;

  if (dec) {
    bool on = final_byte == 'h';
    bool off = final_byte == 'l';
    if (!on && !off) {
      logmsg("DEC command missing on/off terminator: %.*s", fsm->seq.n - 1, fsm->seq.buffer + 1);
      return;
    }
    switch (params[0]) {
    case 25: { /* cursor hidden / shown */
      fsm->opts.cursor_hidden = off;
    } break;
    case 1049: { /* alternate cursor */
      fsm->opts.alternate_screen = on;
      fsm_update_active_grid(fsm);
    } break;
    case 2004: {
      fsm->opts.bracketed_paste = on;
    } break;
    default: {
      logmsg("Unhandled CSI DEC: %.*s", fsm->seq.n - 1, fsm->seq.buffer + 1);
    } break;
    }
    return;
  }

  switch (final_byte) {
  case 'J': { /* Erase in display */
    int mode = params[0];
    struct cursor start = g->cursor;
    struct cursor end = g->cursor;
    switch (mode) {
    case 1: { /* Erase from start of screen to cursor */
      start.x = grid_start(g);
      start.y = grid_top(g);
    } break;
    case 2: { /* Erase entire screen */
      start.x = grid_start(g);
      start.y = grid_top(g);
      end.x = grid_end(g);
      end.y = grid_bottom(g);
    } break;
    case 0:
    default: { /* erase from cursor to end of screen */
      end.x = grid_end(g);
      end.y = grid_bottom(g);
    } break;
    }
    grid_erase_between_cursors(g, start, end);
  } break;
  case 'K': { /* delete operations */
    int mode = params[0];
    struct cursor start = g->cursor;
    struct cursor end = g->cursor;
    switch (mode) {
    case 1: {
      // erase from start to cursor
      start.x = grid_start(g);
    } break;
    case 2: {
      // erase entire line
      start.x = grid_start(g);
      end.x = grid_end(g);
    } break;
    case 0:
    default: {
      // erase from cursor to end
      end.x = grid_end(g);
    } break;
    }
    grid_erase_between_cursors(g, start, end);
  } break;
    /* cursor movement */
  case 'A': { /* move up */
    grid_move_cursor(g, 0, -param1);
  } break;
  case 'B': { /* move down */
    grid_move_cursor(g, 0, param1);
  } break;
  case 'C': { /* move right */
    grid_move_cursor(g, param1, 0);
  } break;
  case 'D': { /* move left */
    grid_move_cursor(g, -param1, 0);
  } break;
  case 'H': { /* cursor move to coordinate */
    int col = params[1];
    int row = params[0];
    grid_set_visual_cursor(g, col, row);
  } break;
  case 'P': { /* delete characters */
    grid_shift_from_cursor(g, param1);
  } break;
  case '@': { /* Insert blank characters */
    grid_insert_blanks_at_cursor(g, param1);
  } break;
  case 'm': { /* color */
  } break;
  case 'r': { /* scroll region */
    int scrollstart = params[0];
    int scrollend = params[1];
    // TODO: this is needed for vim to work correctly.
    // But it requires significant changes
    logmsg("TODO: Handle scroll regions");
  } break;
  default: {
    logmsg("Unhandled CSI: %.*s", fsm->seq.n - 1, fsm->seq.buffer + 1);
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

/* copy to content from one grid to another. This is a naive resizing implementation which just re-inserts everything
 * and counts on the final grid to be accurate */
static void grid_copy(struct grid *restrict dst, const struct grid *const restrict src) {
  for (int i0 = 0; i0 < src->h; i0++) {
    int row = (i0 + src->offset) % src->h;
    int max_col = src->w - 1;

    struct cell *line = &src->cells[row * src->w];

    for (int col = 0; col < src->w && col < line->n_significant; col++) {
      int index = row * src->w + col;
      struct cell c = src->cells[index];
      c.newline = false;
      grid_insert(dst, c, true);
    }
    if (line->newline) {
      grid_newline(dst, true);
    }
    dst->dirty[row] = true;
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

    for (int i = 0; i < h; i++) {
      for (int j = 0; j < w; j++) {
        g->cells[i * w + j] = empty_cell;
      }
    }
  }

  if (g->h != h || g->w != w) {
    struct grid new = {.w = w, .h = h};
    fix_grid(&new, w, h);
    grid_copy(&new, g);
    grid_destroy(g);
    *g = new;
  }
}

static void fix_grids(struct fsm *fsm) {
  fix_grid(&fsm->primary, fsm->w, fsm->h);
  fix_grid(&fsm->alternate, fsm->w, fsm->h);
  fsm_update_active_grid(fsm);
}

static void grid_clear_line(struct grid *g, int line) {
  g->dirty[line] = true;
  for (int i = 0; i < g->w; i++) {
    g->cells[line * g->w + i] = empty_cell;
  }
}

static inline int grid_get_visual_line(struct grid *g) {
  assert(grid_line(g) >= 0 && grid_line(g) < g->h);
  int visual = (g->h + grid_line(g) - g->offset) % g->h;
  assert(visual >= 0 && visual < g->h);
  return visual;
}

static inline void grid_set_visual_line(struct grid *g, int visual) {
  assert(visual >= 0 && visual < g->h);
  int physical = (g->h + visual + g->offset) % g->h;
  assert(physical >= 0 && physical < g->h);
  g->cursor.y = physical;
}

static void grid_set_visual_cursor(struct grid *g, int x, int y) {
  g->cursor.x = CLAMP(x, grid_start(g), grid_end(g));
  int vis = CLAMP(y, grid_top(g), grid_bottom(g));
  grid_set_visual_line(g, vis);
  logmsg("Move cursor to %d;%d", g->cursor.x, vis);
}

static void grid_move_cursor(struct grid *g, int x, int y) {
  // For the 'x' coordinate, the visual and physical coordinates are always synced
  g->cursor.x = CLAMP(g->cursor.x + x, grid_start(g), grid_end(g));

  // This is a bit more convoluted because we need to translate physical / visual coordinates
  int ly = grid_get_visual_line(g);
  ly = CLAMP(ly + y, grid_top(g), grid_bottom(g));
  grid_set_visual_line(g, ly);
}

void grid_invalidate(struct grid *g) {
  if (g) {
    for (int i = 0; i < g->h; i++) {
      g->dirty[i] = true;
    }
  }
}

static void grid_advance_cursor_y_reverse(struct grid *g) {
  int ly = grid_get_visual_line(g);
  if (ly == 0) {
    // TODO: All offset manipulation should be abstracted to a 'scroll_screen' function or something
    // TODO: Grid lines should be abstracted a bit, so instead of indexing a line, one would do grid_get_visual_line
    // to get a cell pointer
    g->offset = (g->h + g->offset - 1) % g->h;
    grid_clear_line(g, g->offset);
    grid_invalidate(g);
  }
  grid_move_cursor(g, 0, -1);
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
   * 1. The width of a cell depends on the content. Some characters are double width. For now, we assume all
   * characters are single width.
   * */
  struct cell *line = grid_current_line(g);
  struct cursor *cur = &g->cursor;
  g->dirty[cur->y] = true;
  g->cells[cur->y * g->w + cur->x] = c;
  line->n_significant = MAX(line->n_significant, cur->x + 1);
  grid_advance_cursor(g, wrap);
}

static void ground_esc(struct fsm *fsm, uint8_t ch) {
  fsm->state = fsm_escape;
  fsm->seq.n = 0;
  fsm->seq.buffer[fsm->seq.n++] = ch;
}
static void ground_noop(struct fsm *fsm, uint8_t ch) {
  (void)fsm, (void)ch;
}

static void grid_carriage_return(struct grid *g) {
  g->cursor.x = 0;
}

static void ground_carriage_return(struct fsm *fsm, uint8_t ch) {
  (void)ch;
  grid_carriage_return(fsm->active_grid);
}

static void grid_backspace(struct grid *g) {
  // Move cursor back, but not past the first column
  g->cursor.x = MAX(0, g->cursor.x - 1);
}

static void ground_backspace(struct fsm *fsm, uint8_t ch) {
  (void)ch;
  grid_backspace(fsm->active_grid);
}
static void ground_vtab(struct fsm *fsm, uint8_t ch) {
  (void)ch;
  grid_advance_cursor_y(fsm->active_grid);
}

static void ground_tab(struct fsm *fsm, uint8_t ch) {
  (void)ch;
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
  (void)fsm, (void)ch;
  write(STDOUT_FILENO, "\a", 1);
}

static void grid_newline(struct grid *g, bool carriage) {
  if (carriage) g->cursor.x = 0;
  struct cell *c = grid_current_line(g);
  c->newline = true;
  grid_advance_cursor_y(g);
}

static void ground_newline(struct fsm *fsm, uint8_t ch) {
  (void)ch;
  grid_newline(fsm->active_grid, !fsm->opts.no_auto_return);
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

  // logmsg("process: %d", ch);

  if (dispatch[ch]) {
    dispatch[ch](fsm, ch);
    return;
  }

  utf8_push(&fsm->cell.symbol, ch);
  uint8_t len = fsm->cell.symbol.len;
  uint8_t exp = fsm->cell.symbol.expected;
  assert(exp <= 4);

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
      case 'M': { /* Cursor up one line, scroll if at top */
        grid_advance_cursor_y_reverse(fsm->active_grid);
        fsm->state = fsm_ground;
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

struct cell *grid_current_line(struct grid *g) {
  return &g->cells[g->cursor.y * g->w];
}
struct cell *grid_current_cell(struct grid *g) {
  return &g->cells[g->cursor.y * g->w + g->cursor.x];
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
