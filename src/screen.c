#include "collections.h"
#include "text.h"
#include "utils.h"
#include <stdlib.h>
#include "screen.h"

#define CLAMP(x, low, high) (MIN(high, MAX(x, low)))
#define screen_column(g) (g->cursor.column)
#define screen_row(g) (&g->rows[g->cursor.row])

/* Missing features:
 * Scrollback
 * Better dirty tracking (maybe that should happen entirely in the renderer)
 */

void screen_clear_line(struct screen *g, int n) {
  struct screen_cell clear = { .symbol = utf8_blank, .style = g->cursor.brush };
  struct screen_row *row = &g->rows[n];
  for (int i = 0; i < g->w; i++) {
    row->cells[i] = clear;
  }
  row->dirty = true;
  row->has_newline = false;
  row->eol = 0;
}

void screen_set_cursor_row(struct screen *g, int y) {
  y = CLAMP(y, screen_top(g), screen_bottom(g));
  g->cursor.row = y;
  g->cursor.wrap_pending = false;
}

void screen_set_cursor_column(struct screen *g, int x) {
  g->cursor.column = CLAMP(x, screen_left(g), screen_right(g));
  g->cursor.wrap_pending = false;
}

void screen_set_cursor_position(struct screen *g, int x, int y) {
  screen_set_cursor_column(g, x);
  screen_set_cursor_row(g, y);
}

void screen_move_cursor_relative(struct screen *g, int x, int y) {
  screen_set_cursor_position(g, g->cursor.column + x, g->cursor.row + y);
}

static void screen_swap_rows(struct screen *g, int r1, int r2) {
  if (r1 != r2) {
    struct screen_row tmp = g->rows[r1];
    g->rows[r1] = g->rows[r2];
    g->rows[r2] = tmp;
    g->rows[r1].dirty = true;
    g->rows[r2].dirty = true;
  }
}


// If the cursor is outside the scroll region, this should do nothing.
// Lines outside the scroll region must not be affected by this.
void screen_insert_lines(struct screen *g, int n) {
  assert(n > 0);
  assert(g->scroll_top >= 0);
  assert(g->scroll_bottom < g->h);
  if (g->cursor.row < g->scroll_top || g->cursor.row > g->scroll_bottom) return;
  screen_shuffle_rows_down(g, n, g->cursor.row, g->scroll_bottom);
}

// If the cursor is outside the scroll region, this should do nothing.
// Lines outside the scroll region must not be affected by this.
void screen_delete_lines(struct screen *g, int n) {
  assert(n > 0);
  assert(g->scroll_top >= 0);
  assert(g->scroll_bottom < g->h);
  if (g->cursor.row < g->scroll_top || g->cursor.row > g->scroll_bottom) return;
  screen_shuffle_rows_up(g, n, g->cursor.row, g->scroll_bottom);
}

void screen_newline(struct screen *g, bool carriage) {
  if (carriage) screen_carriage_return(g);
  struct screen_row *row = screen_row(g);
  row->has_newline = true;
  screen_move_or_scroll_down(g);
}

void screen_scroll_content_down(struct screen *g, int count) {
  screen_shuffle_rows_up(g, count, g->scroll_top, g->scroll_bottom);
}

void screen_move_or_scroll_up(struct screen *g) {
  if (g->cursor.row == g->scroll_top) {
    screen_shuffle_rows_down(g, 1, g->scroll_top, g->scroll_bottom);
  } else {
    screen_move_cursor_relative(g, 0, -1);
  }
}

void screen_scroll_content_up(struct screen *g, int count) {
  screen_shuffle_rows_down(g, count, g->scroll_top, g->scroll_bottom);
}

void screen_move_or_scroll_down(struct screen *g) {
  struct cursor *c = &g->cursor;
  if (c->row == g->scroll_bottom) {
    screen_shuffle_rows_up(g, 1, g->scroll_top, g->scroll_bottom);
  } else {
    c->row = c->row + 1;
  }
  assert(c->row < g->h);
  g->cursor.wrap_pending = false;
}


void screen_restore_cursor(struct screen *g) {
  g->cursor = g->saved_cursor;
}

void screen_save_cursor(struct screen *g) {
  // TODO: This is incorrect. This should save:
  // * cursor position
  // * graphic rendition
  // * character set shift state
  // * state of wrap flag
  // * state of origin mode
  // * state of selective erase
  g->saved_cursor = g->cursor;
}

void screen_insert(struct screen *g, struct screen_cell c, bool wrap) {
  /* Implementation notes:
   * 1. The width of a cell depends on the content. Some characters are double width. For now, we assume all
   * characters are single width.
   * */
  struct cursor *cur = &g->cursor;
  struct screen_row *row = screen_row(g);

  if (g->cursor.wrap_pending) {
  // if (row->end_of_line) {
    g->cursor.wrap_pending = false;
    assert(cur->column == screen_right(g));
    if (wrap) {
      cur->column = 0;
      screen_move_or_scroll_down(g);
      row = screen_row(g);
    } else {
      // Overwrite last character
      cur->column = screen_right(g);
    }
  }

  /* TODO:
   * Rethink conditional redraws. This solution still redraws if a cell is reassigned A -> B -> A
   * Maybe a double buffering strategy is more appropriate.
   */
  row->cells[cur->column] = c;
  row->dirty = true;
  cur->column++;
  row->eol = MAX(row->eol, cur->column);

  if (cur->column > screen_right(g)) {
    cur->wrap_pending = true;
    cur->column = screen_right(g);
  }
}

static void screen_initialize(struct screen *g, int w, int h) {
  g->rows = ecalloc(h, sizeof(*g->rows));
  g->_cells = ecalloc(w * h, sizeof(*g->_cells));
  g->h = h;
  g->w = w;
  screen_reset_scroll_region(g);

  struct screen_cell empty_cell = { .style = style_default, .symbol = utf8_blank };
  for (int i = 0; i < h; i++) {
    g->rows[i] = (struct screen_row){.cells = &g->_cells[i * w], .dirty = true};
    for (int j = 0; j < w; j++) {
      g->_cells[i * w + j] = empty_cell;
    }
  }
}

/* Ensure the screen is able contain the specified number of cells, potentially realloacting it and copying the previous
 * content */
void screen_resize_if_needed(struct screen *g, int w, int h, bool wrap) {
  if (!g->_cells) {
    screen_initialize(g, w, h);
  } else if (g->h != h || g->w != w) {
    struct screen new = {.w = w, .h = h};
    screen_resize_if_needed(&new, w, h, false);
    screen_copy(&new, g, wrap);
    screen_destroy(g);
    *g = new;
  }
}

void screen_reset_scroll_region(struct screen *g) {
  screen_set_scroll_region(g, 0, g->h - 1);
}

void screen_set_scroll_region(struct screen *g, int top, int bottom) {
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
void screen_erase_between_cursors(struct screen *g, struct cursor from, struct cursor to) {
  struct screen_cell template = { .symbol = utf8_blank, .style = g->cursor.brush };

  for (int r = from.row; r <= to.row; r++) {
    struct screen_row *row = &g->rows[r];
    int col_start = r == from.row ? from.column : 0;
    int col_end = r == to.row ? to.column : screen_right(g);
    // We subtract 1 from eol because it refers to the number of significant characters.
    // So if there is 1 significant characters, eol is at 0.
    int eol = MAX(0, row->eol - 1);
    col_end = MIN(col_end, screen_right(g));


    for (int i = col_start; i <= col_end; i++) {
      row->cells[i] = template;
    }

    // If eol was in the range we just erased, update it to be at most the start of the range.
    if (eol >= col_start) {
      // Since col_start was erased, the new eol should
      // at least be the cell preceding it.
      if (eol <= col_end) {
        row->eol = MAX(0, col_start);
      }
    }
    row->dirty = true;
  }
}

void screen_insert_blanks_at_cursor(struct screen *g, int n) {
  struct screen_cell template = { .symbol = utf8_blank, .style = g->cursor.brush };
  struct screen_row *row = screen_row(g);
  int lcol = screen_column(g);
  for (int col = screen_right(g); col >= lcol; col--) {
    int rcol = col - n;
    struct screen_cell replacement = rcol < lcol ? template : row->cells[rcol];
    row->cells[col] = replacement;
  }
  row->eol = MIN(row->eol + n, g->w);
}

void screen_shift_from_cursor(struct screen *g, int n) {
  if (n == 0) return;
  struct screen_cell template = { .symbol = utf8_blank, .style = g->cursor.brush };
  struct screen_row *row = screen_row(g);
  for (int col = screen_column(g); col < screen_right(g); col++) {
    int rcol = col + n;
    struct screen_cell replacement = rcol > screen_right(g) ? template : row->cells[rcol];
    row->cells[col] = replacement;
  }
  row->dirty = true;
  if (row->eol > g->cursor.column) row->eol = MAX(0, row->eol - n);
}
void screen_carriage_return(struct screen *g) {
  screen_set_cursor_column(g, 0);
}
void screen_destroy(struct screen *screen) {
  free(screen->_cells);
  free(screen->rows);
  screen->_cells = NULL;
  screen->rows = NULL;
}
/* copy to content from one screen to another. This is a naive resizing implementation which just re-inserts everything
 * and counts on the final screen to be accurate */
void screen_copy(struct screen *restrict dst, const struct screen *const restrict src, bool wrap) {
  bool fullscreen = !wrap;

  // Preserve the cursor position after the copying operation.
  // This gets a bit complex because it depends how the content was wrapped or truncated.
  // The easiest way is to just put the cursor wherever it is when the cursor from the source screen is encountered.
  struct cursor source_cursor = src->cursor;
  struct cursor dst_cursor = {.row = -1, .column = -1};
  for (int row = 0; row < src->h; row++) {
    struct screen_row *screen_row = &src->rows[row];

    int col = 0;
    for (; col < src->w && col < screen_row->eol; col++) {
      struct screen_cell c = screen_row->cells[col];
      screen_insert(dst, c, wrap);
      if (row == source_cursor.row && col == source_cursor.column) {
        dst_cursor = dst->cursor;
      }
    }

    // If wrapping is not enabled, pad the line with blank cells.
    // This is done to reduce resize artifacts when resizing fullscreen applications
    // with a different background color from the default terminal background
    if (fullscreen) {
      struct screen_cell c = screen_row->cells[src->w - 1];
      c.symbol = utf8_blank;
      for (; col < dst->w; col++) {
        screen_insert(dst, c, false);
        if (row == source_cursor.row && col == source_cursor.column) {
          dst_cursor = dst->cursor;
        }
      }
    }
    if (screen_row->has_newline) {
      screen_newline(dst, true);
    }
    if (row == source_cursor.row && col == source_cursor.column) {
      dst_cursor = dst->cursor;
    }
  }
  for (int i = 0; i < dst->h; i++) dst->rows[i].dirty = true;

  if (dst_cursor.column != -1 && dst_cursor.row != -1) {
    dst->cursor = dst_cursor;
  }
}

void screen_backspace(struct screen *g) {
  screen_set_cursor_column(g, g->cursor.column - 1);
}

void screen_full_reset(struct screen *g) {
  assert(g); assert(g->rows);
  struct cursor clear = {0};
  g->cursor = clear;
  g->saved_cursor = clear;
  struct cursor start = {.column = screen_left(g), .row = screen_top(g)};
  struct cursor end = {.column = screen_right(g), .row = screen_bottom(g)};
  screen_erase_between_cursors(g, start, end);
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

bool cell_equals(const struct screen_cell *const a, const struct screen_cell *const b) {
  return utf8_equals(&a->symbol, &b->symbol) && cell_style_equals(&a->style, &b->style);
}

bool cell_style_equals(const struct screen_cell_style *const a, const struct screen_cell_style *const b) {
  return a->attr == b->attr && color_equals(&a->fg, &b->fg) && color_equals(&a->bg, &b->bg);
}

// Effectively scroll out the bottom `count` rows, leaving `count` clear rows at the top
void screen_shuffle_rows_down(struct screen *g, int count, int top, int bottom) {
  assert(count > 0);
  assert(top >= 0);
  assert(bottom < g->h);
  assert(top <= bottom);

  int n_affected_rows = bottom - top + 1;
  count = MIN(count, n_affected_rows);

  for (int row = bottom; row >= top + count; row--) {
    screen_swap_rows(g, row, row - count);
  }

  // clear the first `count` rows
  for (int row = top; row < top + count; row++) screen_clear_line(g, row);
}

// Effectively scroll out the top `count` rows, leaving `count` clear rows at the bottom.
void screen_shuffle_rows_up(struct screen *g, int count, int top, int bottom) {
  assert(count > 0);
  assert(top >= 0);
  assert(bottom < g->h);
  assert(top <= bottom);

  int n_affected_rows = (bottom + 1) - top;
  count = MIN(count, n_affected_rows);

  for (int row = top; row <= bottom - count; row++) {
    screen_swap_rows(g, row, row + count);
  }

  // clear the final `count` rows
  for (int i = 0; i < count; i++) screen_clear_line(g, bottom - i);
}
