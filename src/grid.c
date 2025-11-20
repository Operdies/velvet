#include "collections.h"
#include "text.h"
#include "utils.h"
#include <stdlib.h>
#include "grid.h"

#define CLAMP(x, low, high) (MIN(high, MAX(x, low)))
#define grid_column(g) (g->cursor.col)
#define grid_row(g) (&g->rows[g->cursor.row])

/* Missing features:
 * Scrollback
 * Better dirty tracking (maybe that should happen entirely in the renderer)
 */


void grid_clear_line(struct grid *g, int n) {
  struct grid_cell clear = { .symbol = utf8_blank, .style = g->cursor.brush };
  struct grid_row *row = &g->rows[n];
  for (int i = 0; i < g->w; i++) {
    row->cells[i] = clear;
  }
  row->dirty = true;
  row->has_newline = false;
  row->eol = 0;
}

void grid_position_cursor_row(struct grid *g, int y) {
  y = CLAMP(y, grid_top(g), grid_bottom(g));
  g->cursor.row = y;
  g->cursor.wrap_pending = false;
}

void grid_position_cursor_column(struct grid *g, int x) {
  g->cursor.col = CLAMP(x, grid_left(g), grid_right(g));
  g->cursor.wrap_pending = false;
}

void grid_position_cursor(struct grid *g, int x, int y) {
  grid_position_cursor_column(g, x);
  grid_position_cursor_row(g, y);
}

void grid_move_cursor(struct grid *g, int x, int y) {
  grid_position_cursor(g, g->cursor.col + x, g->cursor.row + y);
}

void grid_invalidate(struct grid *g) {
  if (g) {
    for (int i = 0; i < g->h; i++) {
      g->rows[i].dirty = true;
    }
  }
}

static void grid_swap_rows(struct grid *g, int r1, int r2) {
  if (r1 != r2) {
    struct grid_row tmp = g->rows[r1];
    g->rows[r1] = g->rows[r2];
    g->rows[r2] = tmp;
    g->rows[r1].dirty = true;
    g->rows[r2].dirty = true;
  }
}


// If the cursor is outside the scroll region, this should do nothing.
// Lines outside the scroll region must not be affected by this.
void grid_insert_lines(struct grid *g, int n) {
  assert(n > 0);
  assert(g->scroll_top >= 0);
  assert(g->scroll_bottom < g->h);
  if (g->cursor.row < g->scroll_top || g->cursor.row > g->scroll_bottom) return;
  int n_affected_rows = g->scroll_bottom - g->cursor.row + 1;
  n = MIN(n, n_affected_rows);
  // Shift the last `n` rows backwards one by one, starting from the back.
  // This will result in all rows after the cursor being shifted forward `n` places,
  // except for the last `n` rows which will be precisely at [row .. row + n],
  // aka the blank lines we need to insert at the cursor.
  for (int row = g->scroll_bottom; row >= g->cursor.row + n; row--) {
    grid_swap_rows(g, row, row - n);
  }

  // clear the first `n` rows
  for (int row = g->cursor.row; row < g->cursor.row + n; row++) {
    grid_clear_line(g, row);
  }
}

// If the cursor is outside the scroll region, this should do nothing.
// Lines outside the scroll region must not be affected by this.
void grid_delete_lines(struct grid *g, int n) {
  assert(n > 0);
  assert(g->scroll_top >= 0);
  assert(g->scroll_bottom < g->h);
  if (g->cursor.row < g->scroll_top || g->cursor.row > g->scroll_bottom) return;
  int rows_after_cursor = g->scroll_bottom + 1;
  int n_affected_rows = rows_after_cursor - g->cursor.row;
  n = MIN(n, n_affected_rows);

  for (int row = g->cursor.row; row < rows_after_cursor - n; row++) {
    grid_swap_rows(g, row, row + n);
  }

  // clear the final `n` rows
  for (int row = rows_after_cursor - n; row < rows_after_cursor; row++) {
    grid_clear_line(g, row);
  }
}

void grid_newline(struct grid *g, bool carriage) {
  if (carriage) grid_carriage_return(g);
  struct grid_row *row = grid_row(g);
  row->has_newline = true;
  grid_move_or_scroll_down(g);
}

static void grid_rotate_rows_forward(struct grid *g) {
  struct grid_row last = g->rows[g->scroll_bottom];
  last.dirty = true;
  for (int i = g->scroll_bottom; i > g->scroll_top; i--) {
    g->rows[i] = g->rows[i-1];
    g->rows[i].dirty = true;
  }
  g->rows[g->scroll_top] = last;
}

/* Get a new row. Right now, that means reusing the first row, but when scrollback is implemented,
 * it will mean managing scrollback and somehow getting a new row. */
static void grid_rotate_rows_reverse(struct grid *g) {
  struct grid_row first = g->rows[g->scroll_top];
  first.dirty = true;
  for (int i = g->scroll_top; i < g->scroll_bottom; i++) {
    g->rows[i] = g->rows[i + 1];
    g->rows[i].dirty = true;
  }
  g->rows[g->scroll_bottom] = first;
}

void grid_move_or_scroll_up(struct grid *g) {
  if (g->cursor.row == g->scroll_top) {
    grid_rotate_rows_forward(g);
    grid_clear_line(g, g->scroll_top);
  } else {
    grid_move_cursor(g, 0, -1);
  }
}

void grid_move_or_scroll_down(struct grid *g) {
  struct cursor *c = &g->cursor;
  if (c->row == g->scroll_bottom) {
    grid_rotate_rows_reverse(g);
    grid_clear_line(g, g->scroll_bottom);
  } else {
    c->row = c->row + 1;
  }
  assert(c->row < g->h);
  g->cursor.wrap_pending = false;
}


void grid_restore_cursor(struct grid *g) {
  g->cursor = g->saved_cursor;
}

void grid_save_cursor(struct grid *g) {
  // TODO: This is incorrect. This should save:
  // * cursor position
  // * graphic rendition
  // * character set shift state
  // * state of wrap flag
  // * state of origin mode
  // * state of selective erase
  g->saved_cursor = g->cursor;
}

void grid_insert(struct grid *g, struct grid_cell c, bool wrap) {
  /* Implementation notes:
   * 1. The width of a cell depends on the content. Some characters are double width. For now, we assume all
   * characters are single width.
   * */
  struct cursor *cur = &g->cursor;
  struct grid_row *row = grid_row(g);

  if (g->cursor.wrap_pending) {
  // if (row->end_of_line) {
    g->cursor.wrap_pending = false;
    assert(cur->col == grid_right(g));
    if (wrap) {
      cur->col = 0;
      grid_move_or_scroll_down(g);
      row = grid_row(g);
    } else {
      // Overwrite last character
      cur->col = grid_right(g);
    }
  }

  /* TODO:
   * Rethink conditional redraws. This solution still redraws if a cell is reassigned A -> B -> A
   * Maybe a double buffering strategy is more appropriate.
   */
  row->cells[cur->col] = c;
  row->dirty = true;
  cur->col++;
  row->eol = MAX(row->eol, cur->col);

  if (cur->col > grid_right(g)) {
    cur->wrap_pending = true;
    cur->col = grid_right(g);
  }
}

/* Ensure the grid is able contain the specified number of cells, potentially realloacting it and copying the previous
 * content */
void grid_resize_if_needed(struct grid *g, int w, int h, bool wrap) {
  if (!g->_cells) {
    grid_initialize(g, w, h);
  } else if (g->h != h || g->w != w) {
    struct grid new = {.w = w, .h = h};
    grid_resize_if_needed(&new, w, h, false);
    grid_copy(&new, g, wrap);
    grid_destroy(g);
    *g = new;
  }
}

void grid_initialize(struct grid *g, int w, int h) {
  g->rows = ecalloc(h, sizeof(*g->rows));
  g->_cells = ecalloc(w * h, sizeof(*g->_cells));
  g->h = h;
  g->w = w;
  grid_reset_scroll_region(g);

  struct grid_cell empty_cell = { .style = style_default, .symbol = utf8_blank };
  for (int i = 0; i < h; i++) {
    g->rows[i] = (struct grid_row){.cells = &g->_cells[i * w], .dirty = true};
    for (int j = 0; j < w; j++) {
      g->_cells[i * w + j] = empty_cell;
    }
  }
}

void grid_reset_scroll_region(struct grid *g) {
  grid_set_scroll_region(g, 0, g->h - 1);
}

void grid_set_scroll_region(struct grid *g, int top, int bottom) {
  if (top < 0) top = 0;
  if (bottom >= g->h) bottom = g->h - 1;

  // the scroll region must contain at least two rows.
  // Since top and bottom are inclusive bounds, that means bottom must be greater than top.
  // If this is not the case, continue without updating the margins.
  if (bottom >= top) {
    g->scroll_top = top;
    g->scroll_bottom = bottom;
  }
}

/* inclusive erase between two cursor positions */
void grid_erase_between_cursors(struct grid *g, struct cursor from, struct cursor to) {
  struct grid_cell template = { .symbol = utf8_blank, .style = g->cursor.brush };

  for (int r = from.row; r <= to.row; r++) {
    int col_start = r == from.row ? from.col : 0;
    int col_end = r == to.row ? to.col : grid_right(g);
    col_end = MIN(col_end, grid_right(g));

    struct grid_row *row = &g->rows[r];

    for (; col_start <= col_end; col_start++) {
      row->cells[col_start] = template;
    }

    // If eol was in the range we just erased, update it to be at most the start of the range.
    if (row->eol <= col_end && row->eol >= col_start) {
      row->eol = MIN(row->eol, col_start);
    }
    row->dirty = true;
  }
}

void grid_insert_blanks_at_cursor(struct grid *g, int n) {
  struct grid_cell template = { .symbol = utf8_blank, .style = g->cursor.brush };
  struct grid_row *row = grid_row(g);
  int lcol = grid_column(g);
  for (int col = grid_right(g); col >= lcol; col--) {
    int rcol = col - n;
    struct grid_cell replacement = rcol < lcol ? template : row->cells[rcol];
    row->cells[col] = replacement;
  }
  row->eol = MIN(row->eol + n, g->w);
}

void grid_shift_from_cursor(struct grid *g, int n) {
  if (n == 0) return;
  struct grid_cell template = { .symbol = utf8_blank, .style = g->cursor.brush };
  struct grid_row *row = grid_row(g);
  for (int col = grid_column(g); col < grid_right(g); col++) {
    int rcol = col + n;
    struct grid_cell replacement = rcol > grid_right(g) ? template : row->cells[rcol];
    row->cells[col] = replacement;
  }
  row->dirty = true;
  if (row->eol > g->cursor.col) row->eol = MAX(0, row->eol - n);
}
void grid_carriage_return(struct grid *g) {
  grid_position_cursor_column(g, 0);
}
void grid_destroy(struct grid *grid) {
  free(grid->_cells);
  free(grid->rows);
  grid->_cells = NULL;
  grid->rows = NULL;
}
/* copy to content from one grid to another. This is a naive resizing implementation which just re-inserts everything
 * and counts on the final grid to be accurate */
void grid_copy(struct grid *restrict dst, const struct grid *const restrict src, bool wrap) {
  bool fullscreen = !wrap;
  for (int row = 0; row < src->h; row++) {

    struct grid_row *grid_row = &src->rows[row];

    int col = 0;
    for (; col < src->w && col < grid_row->eol; col++) {
      struct grid_cell c = grid_row->cells[col];
      grid_insert(dst, c, wrap);
    }

    // If wrapping is not enabled, pad the line with blank cells.
    // This is done to reduce resize artifacts when resizing fullscreen applications
    // with a different background color from the default terminal background
    if (fullscreen) {
      struct grid_cell c = grid_row->cells[src->w - 1];
      c.symbol = utf8_blank;
      for (; col < dst->w; col++) {
        grid_insert(dst, c, false);
      }
    }
    if (grid_row->has_newline) {
      grid_newline(dst, true);
    }
  }
  grid_invalidate(dst);
}

void grid_backspace(struct grid *g) {
  grid_position_cursor_column(g, g->cursor.col - 1);
}

void grid_full_reset(struct grid *g) {
  assert(g); assert(g->rows);
  struct cursor clear = {0};
  g->cursor = clear;
  g->saved_cursor = clear;
  struct cursor start = {.col = grid_left(g), .row = grid_top(g)};
  struct cursor end = {.col = grid_right(g), .row = grid_bottom(g)};
  grid_erase_between_cursors(g, start, end);
}

bool color_equals(const struct color *const a, const struct color *const b) {
  if (a->cmd != b->cmd) return false;
  switch (a->cmd) {
  case COLOR_RESET: return true;
  case COLOR_RGB: return a->r == b->r && a->g == b->g && a->b == b->b;
  case COLOR_TABLE: return a->table == b->table;
  }
  return false;
}

bool cell_equals(const struct grid_cell *const a, const struct grid_cell *const b) {
  return utf8_equals(&a->symbol, &b->symbol) && cell_style_equals(&a->style, &b->style);
}

bool cell_style_equals(const struct grid_cell_style *const a, const struct grid_cell_style *const b) {
  return a->attr == b->attr && color_equals(&a->fg, &b->fg) && color_equals(&a->bg, &b->bg);
}

