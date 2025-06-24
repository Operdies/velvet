#include "emulator.h"
#include "collections.h"
#include "utils.h"
#include <assert.h>
#include <ctype.h>
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
#define PND '#'
#define OSC ']'
#define CHARSET_0 '('
#define CHARSET_1 ')'
#define SI 0x0F
#define SO 0x0E

#define debugthis logmsg("Function: %s, File: %s, Line: %d\n", __func__, __FILE__, __LINE__)

#define grid_start(g) (0)
#define grid_end(g) (g->w - 1)
#define grid_top(g) (0)
#define grid_bottom(g) (g->h - 1)
#define grid_column(g) (g->cursor.x)
#define grid_line(g) (g->cursor.y)
#define grid_row(g) (&g->rows[g->cursor.y])
#define grid_virtual_top(g) (g->offset)
#define grid_virtual_bottom(g) ((g->h + g->offset - 1) % g->h)

static void grid_insert(struct grid *g, struct grid_cell c, bool wrap);
static void grid_destroy(struct grid *grid);
static void grid_move_cursor_x(struct grid *g, int x);
static void grid_move_cursor_y(struct grid *g, int y);
static void grid_move_cursor(struct grid *g, int x, int y);
static void grid_set_visual_cursor(struct grid *g, int x, int y);
static inline struct grid_cell *grid_current_cell(struct grid *g);
static void grid_newline(struct grid *g, bool carriage);
static void grid_advance_cursor_y_reverse(struct grid *g);
static void grid_advance_cursor_y(struct grid *g);
struct grid_cell *grid_current_line(struct grid *g);
static void ensure_grid(struct grid *g, int w, int h, bool reflow);
static void apply_buffered_csi(struct fsm *fsm);

static void fsm_update_active_grid(struct fsm *fsm) {
  struct grid *g = fsm->opts.alternate_screen ? &fsm->alternate : &fsm->primary;
  if (fsm->active_grid != g) {
    fsm->active_grid = g;
    for (int i = 0; i < g->h; i++) g->rows[i].dirty = true;
  }
  // If the primary display is used, this is likely not a tui application,
  // so it is not expected to handle resizes. In that case, we want to re-wrap
  // the currently displayed lines.
  //
  // If it is using the alternate screen, we just want to preserve the current window content
  // in the existing layout until the application redraws its content.
  bool reflow_content = fsm->active_grid == &fsm->primary;
  ensure_grid(fsm->active_grid, fsm->w, fsm->h, reflow_content);
}

static void parse_csi_params(uint8_t *buffer, int len, int *params, int nparams, int *count) {
  *count = 0;
  int i = 0;
  // Skip ahead to first digit
  for (; i < len && !isdigit(buffer[i]); i++);
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
static void grid_erase_between_cursors(struct grid *g, struct raw_cursor from, struct raw_cursor to) {
  int virtual_start = (from.y - g->offset + g->h) % g->h;
  int virtual_end = (to.y - g->offset + g->h) % g->h;

  from.y = virtual_start;
  to.y = virtual_end;

  for (int virtual_y = from.y; virtual_y <= to.y; virtual_y++) {
    int x = virtual_y == from.y ? from.x : 0;
    int end = virtual_y == to.y ? to.x : grid_end(g);

    int raw_y = (virtual_y + g->offset + g->h) % g->h;
    struct grid_row *line = &g->rows[raw_y];

    for (; x <= end; x++) {
      line->cells[x] = empty_cell;
    }

    line->dirty = true;
    if (line->n_significant <= end && line->n_significant >= x) {
      // If the most significant character was deleted, rewind it to at most 'x'
      line->n_significant = MIN(line->n_significant, x);
    } else {
      // Otherwise restore the most significant character
      line->n_significant = line->n_significant;
    }
  }
}

static void grid_insert_blanks_at_cursor(struct grid *g, int n) {
  struct grid_row *row = grid_row(g);
  int lcol = grid_column(g);
  for (int col = grid_end(g); col >= lcol; col--) {
    int rcol = col - n;
    struct grid_cell replacement = rcol < lcol ? empty_cell : row->cells[rcol];
    row->cells[col] = replacement;
  }
  row->n_significant = MIN(row->n_significant + n, g->w);
}

static void grid_shift_from_cursor(struct grid *g, int n) {
  if (n == 0) return;
  struct grid_row *row = grid_row(g);
  for (int col = grid_column(g); col < grid_end(g); col++) {
    int rcol = col + n;
    struct grid_cell replacement = rcol > grid_end(g) ? empty_cell : row->cells[rcol];
    row->cells[col] = replacement;
  }
  row->dirty = true;
  row->end_of_line = false;
  if (row->n_significant > g->cursor.x) row->n_significant = MAX(0, row->n_significant - n);
}

static void grid_set_scroll_region(struct grid *g, int top, int bottom) {
  g->scroll_top = top;
  g->scroll_bottom = bottom;
}

static void process_charset(struct fsm *fsm, unsigned char ch) {
  (void)ch;
  // char *which = fsm->seq.buffer[0] == CHARSET_0 ? &fsm->opts.charset_options.g0 : &fsm->opts.charset_options.g1;
  logmsg("TODO: Handle charsets");
  fsm->state = fsm_ground;
}
static void process_pnd(struct fsm *fsm, unsigned char ch) {
  // All pnd commands are single character commands
  // and can be applied immediately
  fsm->state = fsm_ground;
  switch (ch) {
  case '8': {
    struct grid_cell E = {.symbol = {.len = 1, .utf8 = {'E'}}};
    struct grid *g = fsm->active_grid;
    for (int rowidx = 0; rowidx < g->h; rowidx++) {
      struct grid_row *row = &g->rows[rowidx];
      for (int col = 0; col < g->w; col++) {
        row->cells[col] = E;
      }
      row->n_significant = g->w;
      row->dirty = true;
    }
  } break;
  default: {
    logmsg("TODO: # command: #%c", ch);
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
static void grid_copy(struct grid *restrict dst, const struct grid *const restrict src, bool wrap) {
  for (int i0 = 0; i0 < src->h; i0++) {
    int row = (i0 + src->offset) % src->h;

    struct grid_row *grid_row = &src->rows[row];

    for (int col = 0; col < src->w && col < grid_row->n_significant; col++) {
      struct grid_cell c = grid_row->cells[col];
      grid_insert(dst, c, wrap);
    }
    if (grid_row->newline) {
      grid_newline(dst, true);
    }
  }
  grid_invalidate(dst);
}

static void grid_initialize(struct grid *g, int w, int h) {
  g->rows = calloc(h, sizeof(*g->rows));
  g->cells = calloc(w * h, sizeof(*g->cells));
  g->h = h;
  g->w = w;
  g->scroll_top = 0;
  g->scroll_bottom = h;

  for (int i = 0; i < h; i++) {
    g->rows[i] = (struct grid_row){.cells = &g->cells[i * w], .dirty = true};
    for (int j = 0; j < w; j++) {
      g->cells[i * w + j] = empty_cell;
    }
  }
}

/* Ensure the grid is able contain the specified number of cells, potentially realloacting it and copying the previous
 * content */
static void ensure_grid(struct grid *g, int w, int h, bool wrap) {
  if (!g->cells) {
    grid_initialize(g, w, h);
  } else if (g->h != h || g->w != w) {
    struct grid new = {.w = w, .h = h};
    ensure_grid(&new, w, h, false);
    grid_copy(&new, g, wrap);
    grid_destroy(g);
    *g = new;
  }
}

void fsm_grid_resize(struct fsm *fsm) {
  fsm_update_active_grid(fsm);
}

static void grid_clear_line(struct grid *g, int n) {
  struct grid_row *row = &g->rows[n];
  for (int i = 0; i < g->w; i++) {
    row->cells[i] = empty_cell;
  }
  row->dirty = true;
  row->end_of_line = false;
  row->newline = false;
  row->n_significant = 0;
}

static inline int grid_get_visual_line(struct grid *g) {
  assert(grid_line(g) >= 0 && grid_line(g) < g->h);
  int visual = (g->h + grid_line(g) - g->offset) % g->h;
  assert(visual >= 0 && visual < g->h);
  return visual;
}

static inline void grid_set_visual_line(struct grid *g, int visual) {
  struct grid_row *row = grid_row(g);
  row->end_of_line = false;
  assert(visual >= 0 && visual < g->h);
  int physical = (g->h + visual + g->offset) % g->h;
  assert(physical >= 0 && physical < g->h);
  g->cursor.y = physical;
}

static void grid_set_visual_cursor(struct grid *g, int x, int y) {
  struct grid_row *row = grid_row(g);
  row->end_of_line = false;
  g->cursor.x = CLAMP(x, grid_start(g), grid_end(g));
  int vis = CLAMP(y, grid_top(g), grid_bottom(g));
  grid_set_visual_line(g, vis);
}

static inline void grid_set_cursor_y(struct grid *g, int y) {
  struct grid_row *row = grid_row(g);
  row->end_of_line = false;

  // Translate raw coordinates to visual coordinates
  y = CLAMP(y, grid_top(g), grid_bottom(g));
  grid_set_visual_line(g, y);
}

static inline void grid_set_cursor_x(struct grid *g, int x) {
  struct grid_row *row = grid_row(g);
  row->end_of_line = false;
  g->cursor.x = CLAMP(x, grid_start(g), grid_end(g));
}

static void grid_move_cursor_x(struct grid *g, int x) {
  struct grid_row *row = grid_row(g);
  row->end_of_line = false;

  // For the 'x' coordinate, the visual and physical coordinates are always synced
  g->cursor.x = CLAMP(g->cursor.x + x, grid_start(g), grid_end(g));
}

static void grid_move_cursor_y(struct grid *g, int y) {
  struct grid_row *row = grid_row(g);
  row->end_of_line = false;

  // This is a bit more convoluted because we need to translate physical / visual coordinates
  int ly = grid_get_visual_line(g);
  ly = CLAMP(ly + y, grid_top(g), grid_bottom(g));
  grid_set_visual_line(g, ly);
}

static void grid_move_cursor(struct grid *g, int x, int y) {
  grid_move_cursor_x(g, x);
  grid_move_cursor_y(g, y);
}

void grid_invalidate(struct grid *g) {
  if (g) {
    for (int i = 0; i < g->h; i++) {
      g->rows[i].dirty = true;
    }
  }
}

static void grid_shift_lines(struct grid *g, int n) {
  int rows_after_point = g->h - grid_get_visual_line(g);
  n = MIN(n, rows_after_point);

  int shift = n * g->w;
  int point = g->cursor.y * g->w;
  int total_cells_in_grid = g->w * g->h;
  int cells_after_point = rows_after_point * g->w;

  int cell = 0;
  // As long as cell + shift is within the bounds of the grid, copy back
  for (; (cell + shift) < cells_after_point; cell++) {
    struct grid_cell *dst = &g->cells[(cell + point) % total_cells_in_grid];
    struct grid_cell *src = &g->cells[(cell + point + shift) % total_cells_in_grid];
    *dst = *src;
  }

  // If cell + shift is out of range of the grid, clear the cell
  for (; cell < cells_after_point; cell++) {
    struct grid_cell *dst = &g->cells[(cell + point) % total_cells_in_grid];
    *dst = empty_cell;
  }

  // Dirty all lines affected by the shift
  for (int row = 0; row < rows_after_point; row++) {
    int row_offset = (row + g->cursor.y) % g->h;
    g->rows[row_offset].dirty = true;
  }
}

static void grid_advance_cursor_y_reverse(struct grid *g) {
  struct grid_row *row = grid_row(g);
  row->end_of_line = false;
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
  struct grid_row *row = grid_row(g);
  row->end_of_line = false;
  struct raw_cursor *c = &g->cursor;
  c->y = (c->y + 1) % g->h;
  if (c->y == g->offset) {
    grid_clear_line(g, g->offset);
    g->offset = (g->offset + 1) % g->h;
    grid_invalidate(g);
  }
  assert(c->y < g->h);
}

static void grid_insert(struct grid *g, struct grid_cell c, bool wrap) {
  /* Implementation notes:
   * 1. The width of a cell depends on the content. Some characters are double width. For now, we assume all
   * characters are single width.
   * */
  struct raw_cursor *cur = &g->cursor;
  struct grid_row *row = grid_row(g);

  if (row->end_of_line) {
    row->end_of_line = false;
    assert(cur->x == grid_end(g));
    if (wrap) {
      cur->x = 0;
      cur->y = (cur->y + 1) % g->h;
      if (cur->y == g->offset) {
        grid_clear_line(g, g->offset);
        g->offset = (g->offset + 1) % g->h;
        grid_invalidate(g);
      }
      row = grid_row(g);
    } else {
      // Overwrite last character
      cur->x = grid_end(g);
    }
  }

  row->cells[cur->x] = c;
  cur->x++;
  row->n_significant = MAX(row->n_significant, cur->x);

  if (cur->x > grid_end(g)) {
    row->end_of_line = true;
    cur->x = grid_end(g);
  }
  row->dirty = true;
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
  struct grid_row *row = grid_row(g);
  row->end_of_line = false;
  grid_set_cursor_x(g, 0);
}

static void ground_carriage_return(struct fsm *fsm, uint8_t ch) {
  (void)ch;
  grid_carriage_return(fsm->active_grid);
}

static void grid_backspace(struct grid *g) {
  grid_set_cursor_x(g, g->cursor.x - 1);
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
  struct grid_cell c = fsm->cell;
  c.symbol = utf8_blank;
  grid_insert(fsm->active_grid, c, !fsm->opts.nowrap);
  for (int i = 1; i < numSpaces; i++) {
    grid_insert(fsm->active_grid, c, false);
  }
}

static void ground_bell(struct fsm *fsm, uint8_t ch) {
  (void)fsm, (void)ch;
  write(STDOUT_FILENO, "\a", 1);
}

static void grid_newline(struct grid *g, bool carriage) {
  if (carriage) grid_carriage_return(g);
  struct grid_row *row = grid_row(g);
  row->newline = true;
  grid_advance_cursor_y(g);
}

static void ground_newline(struct fsm *fsm, uint8_t ch) {
  (void)ch;
  grid_newline(fsm->active_grid, fsm->opts.auto_return);
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
  struct grid_cell replacement = {.symbol = utf8_fffd};
  struct grid *g = fsm->active_grid;
  grid_insert(g, replacement, !fsm->opts.nowrap);
  if (copy.len > 1) fsm_process(fsm, &copy.utf8[1], copy.len - 1);
}

static inline uint8_t utf8_expected_length(uint8_t ch) {
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
    uint8_t expected_length = utf8_expected_length(byte);
    u->expected = expected_length;
  }
  u->utf8[u->len] = byte;
  u->len++;
}

static void ground_process_shift_in_out(struct fsm *fsm, uint8_t ch) {
  fsm->opts.charset_options.g1_active = ch == SO;
}

static void process_ground(struct fsm *fsm, uint8_t ch) {
  // These symbols have special behavior in terms of how they affect layout
  static void (*const dispatch[UINT8_MAX])(struct fsm *, uint8_t) = {
      [NUL] = ground_noop,
      [ESC] = ground_esc,
      [RET] = ground_carriage_return,
      [BSP] = ground_backspace,
      [BELL] = ground_bell,
      [SI] = ground_process_shift_in_out,
      [SO] = ground_process_shift_in_out,
      [DEL] = ground_noop,
      [TAB] = ground_tab,
      [VTAB] = ground_vtab,
      [FORMFEED] = ground_newline,
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
  fsm_grid_resize(fsm);
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
      case CHARSET_0:
      case CHARSET_1: {
        fsm->state = fsm_charset;
        fsm->seq.buffer[fsm->seq.n++] = ch;
      } break;
      case PND: {
        fsm->state = fsm_pnd;
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
        logmsg("Save Cursor");
      } break;
      case '8': {
        fsm->state = fsm_ground;
        fsm->active_grid->cursor = fsm->active_grid->saved_cursor;
        logmsg("Restore Cursor");
      } break;
      case 'M': { /* Cursor up one line, scroll if at top */
        logmsg("scroll reverse");
        grid_advance_cursor_y_reverse(fsm->active_grid);
        fsm->state = fsm_ground;
      } break;
      case '=': { /* Enable application keypad mode */
        fsm->opts.application_keypad_mode = true;
      } break;
      case '>': { /* Disable application keypad mode (enable numeric keypad mode) */
        fsm->opts.application_keypad_mode = false;
      } break;
      case ESC: { /* Escaped literal escape */
        utf8_push(&fsm->cell.symbol, ESC);
        ground_accept(fsm);
        fsm->state = fsm_ground;
      } break;
      default: {
        // Unrecognized escape. Treat this char as escaped and continue parsing normally.
        logmsg("Unrecognized basic escape: 0x%x", ch);
        fsm->state = fsm_ground;
        break;
      }
      }
    } break;
    case fsm_dcs: {
      char prev = fsm->seq.n > 1 ? fsm->seq.buffer[fsm->seq.n - 1] : 0;
      fsm->seq.buffer[fsm->seq.n++] = ch;
      if (ch == '\\' && prev == ESC) {
        fsm->state = fsm_ground;
        logmsg("TODO: DCS sequence: '%.*s'", fsm->seq.n - 2, fsm->seq.buffer);
      } else if (fsm->seq.n >= MAX_ESC_SEQ_LEN) {
        fsm->state = fsm_ground;
        logmsg("Abort DCS: max length exceeded");
      }
    } break;
    case fsm_osc: {
      char prev = fsm->seq.n > 1 ? fsm->seq.buffer[fsm->seq.n - 1] : 0;
      fsm->seq.buffer[fsm->seq.n++] = ch;
      if (ch == BELL || (ch == '\\' && prev == ESC)) {
        logmsg("TODO: OSC sequence: %.*s", fsm->seq.n - 1, fsm->seq.buffer);
        fsm->state = fsm_ground;
      } else if (fsm->seq.n >= MAX_ESC_SEQ_LEN) {
        fsm->state = fsm_ground;
        logmsg("Abort OSC: max length exceeded");
      }
    } break;
    case fsm_csi: {
      process_csi(fsm, ch);
    } break;
    case fsm_pnd: {
      process_pnd(fsm, ch);
    } break;
    case fsm_charset: {
      process_charset(fsm, ch);
    } break;
    default: {
      assert(!"Unreachable");
    }
    }
  }
}

struct grid_cell *grid_current_cell(struct grid *g) {
  int idx = g->cursor.y * g->w + g->cursor.x;
  assert(idx >= 0 && idx < g->w * g->h);
  struct grid_cell *c = &g->cells[idx];
  assert(c);
  return c;
}

void grid_destroy(struct grid *grid) {
  free(grid->cells);
  free(grid->rows);
  grid->cells = NULL;
  grid->rows = NULL;
}

void fsm_destroy(struct fsm *fsm) {
  grid_destroy(&fsm->primary);
  grid_destroy(&fsm->alternate);
}

static void csi_toggle_mode(struct fsm *fsm, int n, int params[n]) {
  uint8_t *buffer = fsm->seq.buffer;
  size_t len = fsm->seq.n;
  uint8_t final_byte = buffer[len - 1];
  bool on = final_byte == 'h';
  bool off = final_byte == 'l';
  switch (params[0]) {
  case 1: { /* Application mode */
    fsm->opts.application_mode = on;
  } break;
  case 4: { /* Set insert/replace mode */
  } break;
  case 7: { /* Set automatic line wrapping mode */
    logmsg("Set wrapping: %s", on ? "on" : "off");
    fsm->opts.nowrap = off;
  } break;
  case 12: { /* Set local echo mode -- This is safe to ignore. */
  } break;
  case 20: { /* Set carriage return mode */
    fsm->opts.auto_return = on;
  } break;
  case 25: { /* Set cursor visible mode */
    fsm->opts.cursor_hidden = off;
  } break;
  case 1004: { /* Set focus reporting mode */
    fsm->opts.focus_reporting = on;
  } break;
  case 1049: { /* alternate screen */
    fsm->opts.alternate_screen = on;
    fsm_update_active_grid(fsm);
  } break;
  case 2004: {
    fsm->opts.bracketed_paste = on;
    logmsg("TODO: Bracketed paste");
  } break;
  default: {
    logmsg("TODO: CSI DEC: %.*s", fsm->seq.n - 1, fsm->seq.buffer + 1);
  } break;
  }
  return;
}

static void csi_query_modifiers(struct fsm *fsm, int n, int params[n]) {
  logmsg("TODO: Implement query modifiers");
  if (n == 0) {
    fsm->opts.modifier_options = (struct modifier_options){0};
  } else {
    int options = params[0];
    if (options >= 0 && options <= 7) {
      fsm->opts.modifier_options.options[options] = params[1];
    }
  }
}

static void csi_set_modifiers(struct fsm *fsm, int n, int params[n]) {
  (void)fsm, (void)n, (void)params;
  logmsg("TODO: Implement set modifiers");
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
  int param1 = count > 0 ? params[0] : 1;

  if (buffer[2] == '?' && (final_byte == 'h' || final_byte == 'l')) {
    csi_toggle_mode(fsm, count, params);
    return;
  }
  if (buffer[2] == '>' && final_byte == 'm') {
    csi_set_modifiers(fsm, count, params);
    return;
  }
  if (buffer[2] == '?' && final_byte == 'm') {
    csi_query_modifiers(fsm, count, params);
    return;
  }

  switch (final_byte) {
  case 'J': { /* Erase in display */
    int mode = params[0];
    struct raw_cursor start = g->cursor;
    struct raw_cursor end = g->cursor;
    switch (mode) {
    case 1: { /* Erase from start of screen to cursor */
      start.x = grid_start(g);
      start.y = grid_virtual_top(g);
    } break;
    case 2: { /* Erase entire screen */
      start.x = grid_start(g);
      start.y = grid_virtual_top(g);
      end.x = grid_end(g);
      end.y = grid_virtual_bottom(g);
    } break;
    case 3: { /* erase scrollback */
      return;
    } break;
    case 0:
    default: { /* erase from cursor to end of screen */
      end.x = grid_end(g);
      end.y = grid_virtual_bottom(g);
    } break;
    }
    grid_erase_between_cursors(g, start, end);
  } break;
  case 't': {
    /* xterm extensions for saving / restoring icon / window title
     * These are used by vim so we should probably support them.
     */
    if (params[0] == 22) {
      if (params[1] == 1) { /* Save Screen Icon */
        logmsg("TODO: Save Icon");
      } else if (params[1] == 2) { /* Cursor Save Title */
        logmsg("TODO: Save Title");
      }
    } else if (params[0] == 23) {
      if (params[1] == 1) { /* Restore Screen Icon */
        logmsg("TODO: Restore Icon");
      } else if (params[1] == 2) { /* Cursor Restore Title */
        logmsg("TODO: Restore Title");
      }
    }
    logmsg("TODO: extension: %.*s", fsm->seq.n - 1, fsm->seq.buffer + 1);
  } break;
  case 'K': { /* delete operations */
    int mode = params[0];
    struct raw_cursor start = g->cursor;
    struct raw_cursor end = g->cursor;
    switch (mode) {
    case 1: { /* erase from start to cursor */
      start.x = grid_start(g);
    } break;
    case 2: { /* erase entire line */
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
    int col = params[1] ? params[1] : 1;
    int row = params[0] ? params[0] : 1;
    grid_set_visual_cursor(g, col - 1, row - 1);
  } break;
  case 'P': { /* delete characters */
    grid_shift_from_cursor(g, param1);
  } break;
  case '@': { /* Insert blank characters */
    grid_insert_blanks_at_cursor(g, param1);
  } break;
  case 'n': { /* report cursor position */
    if (fsm->on_report_cursor_position) fsm->on_report_cursor_position(fsm->context, g->cursor.y + 1, g->cursor.x + 1);
  } break;
  case 'd': { /* move to absolute row */
    int row = params[0] ? params[0] : 1;
    grid_set_cursor_y(g, row - 1);
  } break;
  case 'G': { /* move to absolute column */
    int col = params[0] ? params[0] : 1;
    grid_set_cursor_x(g, col - 1);
  } break;
  case 'b': { /* repeat last character */
    int count = params[0] ? params[0] : 1;
    struct grid_cell repeat = fsm->cell;
    if (repeat.symbol.len == 0) repeat.symbol = utf8_blank;
    for (int i = 0; i < count; i++) {
      grid_insert(g, repeat, !fsm->opts.nowrap);
    }
  } break;
  case 'L': { /* Insert blanks lines at cursor */
    logmsg("TODO: Implement CSI L");
  } break;
  case 'M': { /* shift next Pn up */
    int count = params[0] ? params[0] : 1;
    grid_shift_lines(g, count);
  } break;
  case 'm': { /* color */
    struct grid_cell *c = grid_current_cell(g);
    assert(c);
    switch (params[0]) {
    case 0: { /* reset */
      c->fg = 0, c->bg = 0, c->attr = 0;
    } break;
    default: {
      // logmsg("TODO: SGR: %.*s", fsm->seq.n - 1, fsm->seq.buffer + 1);
    } break;
    }
  } break;
  case 'r': { /* scroll region */
    logmsg("TODO: Implement scroll region");
    int top = count > 0 ? params[0] : 1;
    int bottom = count > 1 ? params[1] : g->h;
    top = MAX(top, 1);
    bottom = MIN(bottom, g->h);
    if (top <= bottom) {
      logmsg("Scroll region requested: %d - %d", top, bottom);
      grid_set_scroll_region(g, top, bottom);
    } else {
      logmsg("Invalid scroll region requested: %d - %d", top, bottom);
    }
  } break;
  default: {
    logmsg("TODO: CSI: %.*s", fsm->seq.n - 1, fsm->seq.buffer + 1);
  } break;
  }
}
