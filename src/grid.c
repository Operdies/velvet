#include "grid.h"
#include "collections.h"
#include "text.h"
#include "utils.h"
#include <stdlib.h>

#define CLAMP(x, low, high) (MIN(high, MAX(x, low)))

/* Missing features:
 * Scrollback
 * Better dirty tracking (maybe that should happen entirely in the renderer)
 */

void grid_clear_line(struct grid *g, int n, struct grid_cell_style style) {
  struct grid_cell template = { .symbol = utf8_blank, .style = style };
  struct grid_row *row = &g->rows[n];
  for (int i = 0; i < g->w; i++) {
    row->cells[i] = template;
  }
  row->dirty = true;
  row->end_of_line = false;
  row->newline = false;
  row->n_significant = 0;
}

int grid_get_visual_line(struct grid *g) {
  assert(grid_line(g) >= 0 && grid_line(g) < g->h);
  int visual = (g->h + grid_line(g) - g->offset) % g->h;
  assert(visual >= 0 && visual < g->h);
  return visual;
}

void grid_set_visual_line(struct grid *g, int visual) {
  struct grid_row *row = grid_row(g);
  row->end_of_line = false;
  assert(visual >= 0 && visual < g->h);
  int physical = (g->h + visual + g->offset) % g->h;
  assert(physical >= 0 && physical < g->h);
  g->cursor.row = physical;
}

void grid_position_visual_cursor(struct grid *g, int x, int y) {
  struct grid_row *row = grid_row(g);
  row->end_of_line = false;
  g->cursor.col = CLAMP(x, grid_start(g), grid_end(g));
  int vis = CLAMP(y, grid_top(g), grid_bottom(g));
  grid_set_visual_line(g, vis);
}

void grid_position_cursor_row(struct grid *g, int y) {
  struct grid_row *row = grid_row(g);
  row->end_of_line = false;

  // Translate raw coordinates to visual coordinates
  y = CLAMP(y, grid_top(g), grid_bottom(g));
  grid_set_visual_line(g, y);
}

void grid_position_cursor_column(struct grid *g, int x) {
  struct grid_row *row = grid_row(g);
  row->end_of_line = false;
  g->cursor.col = CLAMP(x, grid_start(g), grid_end(g));
}

static void grid_increment_cursor_column(struct grid *g, int x) {
  struct grid_row *row = grid_row(g);
  row->end_of_line = false;
  g->cursor.col = CLAMP(g->cursor.col + x, grid_start(g), grid_end(g));
}

static void grid_increment_cursor_row(struct grid *g, int y) {
  struct grid_row *row = grid_row(g);
  row->end_of_line = false;

  // This is a bit more convoluted because we need to translate physical / visual coordinates
  int ly = grid_get_visual_line(g);
  ly = CLAMP(ly + y, grid_top(g), grid_bottom(g));
  grid_set_visual_line(g, ly);
}

void grid_move_cursor(struct grid *g, int x, int y) {
  grid_increment_cursor_column(g, x);
  grid_increment_cursor_row(g, y);
}

void grid_invalidate(struct grid *g) {
  if (g) {
    for (int i = 0; i < g->h; i++) {
      g->rows[i].dirty = true;
    }
  }
}

static void grid_shift_lines_reverse(struct grid *g, int n, struct grid_cell_style style) {
  // Make n rows of empty space
  // Then shift everything
  struct grid_cell template = { .style = style, .symbol = utf8_blank };

  int rows_after_point = g->h - grid_get_visual_line(g);
  n = MIN(n, rows_after_point);
  int shift = n * g->w;
  int point = g->cursor.row * g->w;
  int total_cells_in_grid = g->w * g->h;
  int cells_after_point = rows_after_point * g->w;

  int cell = 0;
  // As long as cell + shift is within the grid, copy forward
  for (cell = cells_after_point - 1; cell >= shift; cell--) {
    struct grid_cell *dst = &g->cells[(cell + point) % total_cells_in_grid];
    struct grid_cell *src = &g->cells[(cell + point - shift) % total_cells_in_grid];
    *dst = *src;
  }

  // Now clear the first n lines
  for (cell = 0; cell < shift; cell++) {
    struct grid_cell *dst = &g->cells[(cell + point) % total_cells_in_grid];
    *dst = template;
  }

  // Dirty all lines affected by the shift
  for (int row = 0; row < rows_after_point; row++) {
    int row_offset = (row + g->cursor.row) % g->h;
    g->rows[row_offset].dirty = true;
  }
}

void grid_shift_lines(struct grid *g, int n, struct grid_cell_style style) {
  if (n < 0) {
    grid_shift_lines_reverse(g, -n, style);
    return;
  }
  struct grid_cell template = { .style = style, .symbol = utf8_blank };
  int rows_after_point = g->h - grid_get_visual_line(g);
  n = MIN(n, rows_after_point);

  int shift = n * g->w;
  int point = g->cursor.row * g->w;
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
    *dst = template;
  }

  // Dirty all lines affected by the shift
  for (int row = 0; row < rows_after_point; row++) {
    int row_offset = (row + g->cursor.row) % g->h;
    g->rows[row_offset].dirty = true;
  }
}

void grid_newline(struct grid *g, bool carriage, struct grid_cell_style style) {
  if (carriage) grid_carriage_return(g);
  struct grid_row *row = grid_row(g);
  row->newline = true;
  grid_advance_cursor_y(g, style);
}

void grid_advance_cursor_y_reverse(struct grid *g, struct grid_cell_style style) {
  struct grid_row *row = grid_row(g);
  row->end_of_line = false;
  int ly = grid_get_visual_line(g);
  if (ly == 0) {
    g->offset = (g->h + g->offset - 1) % g->h;
    grid_clear_line(g, g->offset, style);
    grid_invalidate(g);
  }
  grid_move_cursor(g, 0, -1);
}

void grid_restore_cursor(struct grid *g) {
  struct grid_row *row = grid_row(g);
  row->end_of_line = false;
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
  struct grid_row *row = grid_row(g);
  row->end_of_line = false;
  g->saved_cursor = g->cursor;
}

void grid_advance_cursor_y(struct grid *g, struct grid_cell_style style) {
  struct grid_row *row = grid_row(g);
  row->end_of_line = false;
  struct raw_cursor *c = &g->cursor;
  c->row = (c->row + 1) % g->h;
  if (c->row == g->offset) {
    grid_clear_line(g, g->offset, style);
    g->offset = (g->offset + 1) % g->h;
    grid_invalidate(g);
  }
  assert(c->row < g->h);
}

void grid_insert(struct grid *g, struct grid_cell c, bool wrap) {
  /* Implementation notes:
   * 1. The width of a cell depends on the content. Some characters are double width. For now, we assume all
   * characters are single width.
   * */
  struct raw_cursor *cur = &g->cursor;
  struct grid_row *row = grid_row(g);

  if (row->end_of_line) {
    row->end_of_line = false;
    assert(cur->col == grid_end(g));
    if (wrap) {
      cur->col = 0;
      cur->row = (cur->row + 1) % g->h;
      if (cur->row == g->offset) {
        grid_clear_line(g, g->offset, c.style);
        g->offset = (g->offset + 1) % g->h;
        grid_invalidate(g);
      }
      row = grid_row(g);
    } else {
      // Overwrite last character
      cur->col = grid_end(g);
    }
  }

  /* TODO:
   * Rethink conditional redraws. This solution still redraws if a cell is reassigned A -> B -> A
   * Maybe a double buffering strategy is more appropriate.
   */
  row->cells[cur->col] = c;
  row->dirty = true;
  cur->col++;
  row->n_significant = MAX(row->n_significant, cur->col);

  if (cur->col > grid_end(g)) {
    row->end_of_line = true;
    cur->col = grid_end(g);
  }
}

/* Ensure the grid is able contain the specified number of cells, potentially realloacting it and copying the previous
 * content */
void grid_resize_if_needed(struct grid *g, int w, int h, bool wrap) {
  if (!g->cells) {
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
  g->cells = ecalloc(w * h, sizeof(*g->cells));
  g->h = h;
  g->w = w;
  grid_set_scroll_region(g, 0, h - 1);

  struct grid_cell empty_cell = { .style = style_default, .symbol = utf8_blank };
  for (int i = 0; i < h; i++) {
    g->rows[i] = (struct grid_row){.cells = &g->cells[i * w], .dirty = true};
    for (int j = 0; j < w; j++) {
      g->cells[i * w + j] = empty_cell;
    }
  }
}

void grid_set_scroll_region(struct grid *g, int top, int bottom) {
  g->scroll_top = top;
  g->scroll_bottom = bottom;
}

/* inclusive erase between two cursor positions */
void grid_erase_between_cursors(struct grid *g, struct raw_cursor from, struct raw_cursor to, struct grid_cell_style style) {
  int virtual_start = (from.row - g->offset + g->h) % g->h;
  int virtual_end = (to.row - g->offset + g->h) % g->h;
  struct grid_cell template = { .symbol = utf8_blank, .style = style };

  from.row = virtual_start;
  to.row = virtual_end;

  for (int virtual_row = from.row; virtual_row <= to.row; virtual_row++) {
    int col_start = virtual_row == from.row ? from.col : 0;
    int col_end = virtual_row == to.row ? to.col : grid_end(g);
    col_end = MIN(col_end, grid_end(g));

    int row = (virtual_row + g->offset + g->h) % g->h;
    struct grid_row *line = &g->rows[row];

    for (; col_start <= col_end; col_start++) {
      line->cells[col_start] = template;
    }

    line->dirty = true;
    if (line->n_significant <= col_end && line->n_significant >= col_start) {
      // If the most significant character was deleted, rewind it to at most 'x'
      line->n_significant = MIN(line->n_significant, col_start);
    } else {
      // Otherwise restore the most significant character
      line->n_significant = line->n_significant;
    }
  }
}

void grid_insert_blanks_at_cursor(struct grid *g, int n, struct grid_cell_style style) {
  struct grid_cell template = { .symbol = utf8_blank, .style = style };
  struct grid_row *row = grid_row(g);
  int lcol = grid_column(g);
  for (int col = grid_end(g); col >= lcol; col--) {
    int rcol = col - n;
    struct grid_cell replacement = rcol < lcol ? template : row->cells[rcol];
    row->cells[col] = replacement;
  }
  row->n_significant = MIN(row->n_significant + n, g->w);
}

void grid_shift_from_cursor(struct grid *g, int n, struct grid_cell_style style) {
  if (n == 0) return;
  struct grid_cell template = { .symbol = utf8_blank, .style = style };
  struct grid_row *row = grid_row(g);
  for (int col = grid_column(g); col < grid_end(g); col++) {
    int rcol = col + n;
    struct grid_cell replacement = rcol > grid_end(g) ? template : row->cells[rcol];
    row->cells[col] = replacement;
  }
  row->dirty = true;
  row->end_of_line = false;
  if (row->n_significant > g->cursor.col) row->n_significant = MAX(0, row->n_significant - n);
}
void grid_carriage_return(struct grid *g) {
  struct grid_row *row = grid_row(g);
  row->end_of_line = false;
  grid_position_cursor_column(g, 0);
}
void grid_destroy(struct grid *grid) {
  free(grid->cells);
  free(grid->rows);
  grid->cells = NULL;
  grid->rows = NULL;
}
/* copy to content from one grid to another. This is a naive resizing implementation which just re-inserts everything
 * and counts on the final grid to be accurate */
void grid_copy(struct grid *restrict dst, const struct grid *const restrict src, bool wrap) {
  bool fullscreen = !wrap;
  for (int i0 = 0; i0 < src->h; i0++) {
    int row = (i0 + src->offset) % src->h;

    struct grid_row *grid_row = &src->rows[row];

    int col = 0;
    for (; col < src->w && col < grid_row->n_significant; col++) {
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
    if (grid_row->newline) {
      grid_newline(dst, true, style_default);
    }
  }
  grid_invalidate(dst);
}

void grid_backspace(struct grid *g) {
  grid_position_cursor_column(g, g->cursor.col - 1);
}

void grid_full_reset(struct grid *g) {
  struct grid_row *row = grid_row(g);
  row->end_of_line = false;
  struct raw_cursor start = {.col = grid_start(g), .row = grid_virtual_top(g)};
  struct raw_cursor end = {.col = grid_end(g), .row = grid_virtual_bottom(g)};
  grid_erase_between_cursors(g, start, end, style_default);
  g->cursor.col = 0;
  g->cursor.row = 0;
}
