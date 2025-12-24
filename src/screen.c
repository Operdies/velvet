#include "collections.h"
#include "text.h"
#include "utils.h"
#include <stdlib.h>
#include "screen.h"
#include <wchar.h>

#define screen_column(g) (g->cursor.column)
#define screen_row(g) (&g->lines[g->cursor.line])

bool screen_scrollback_pop(struct screen_scrollback *s, struct screen_line *l) {
  assert(s->enabled);
  struct scrollback_line *last = vec_pop(&s->lines);
  if (last) {
    struct screen_cell *start = vec_nth_unchecked(&s->cells, last->cell_offset);
    l->cells = start;
    l->has_newline = last->has_newline;
    l->eol = last->length;
    vec_truncate(&s->cells, s->cells.length - last->length);
    return true;
  }
  return false;
}

void screen_scrollback_push(struct screen_scrollback *s, struct screen_line *l) {
  assert(s->enabled);
  if (l->has_newline) {
    /* trim trailing spaces. */
    while (l->eol && l->cells[l->eol-1].cp.value == ' ') l->eol--;
  }
  vec_push_range(&s->cells, l->cells, l->eol);
  if (s->lines.length) {
    /* if the previous line was not terminated, append this line to the previous one */
    struct scrollback_line *prev = vec_nth(&s->lines, s->lines.length - 1);
    if (!prev->has_newline) {
      prev->length += l->eol;
      prev->has_newline = l->has_newline;
      return;
    }
  }

  struct scrollback_line *new = vec_new_element(&s->lines);
  new->has_newline = l->has_newline;
  new->cell_offset = s->cells.length - l->eol;
  new->length = l->eol;

  static struct vec debug = vec(struct screen_line);
  for (size_t i = 0; i < s->lines.length; i++) {
    struct scrollback_line *last = vec_nth(&s->lines, i);
    struct screen_cell *start = vec_nth_unchecked(&s->cells, last->cell_offset);
    struct screen_line l2 = { .cells = start, .has_newline = last->has_newline, .eol = last->length };
    vec_push(&debug, &l2);
  }
  vec_clear(&debug);
}

void screen_clear_line(struct screen *g, int n) {
  struct screen_cell clear = { .cp = codepoint_space, .style = g->cursor.brush };
  struct screen_line *row = &g->lines[n];
  for (int i = 0; i < g->w; i++) {
    row->cells[i] = clear;
  }
  row->has_newline = false;
  row->eol = 0;
}

void screen_set_cursor_row(struct screen *g, int y) {
  y = CLAMP(y, screen_top(g), screen_bottom(g));
  g->cursor.line = y;
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
  screen_set_cursor_position(g, g->cursor.column + x, g->cursor.line + y);
}

static void screen_swap_rows(struct screen *g, int r1, int r2) {
  if (r1 != r2) {
    struct screen_line tmp = g->lines[r1];
    g->lines[r1] = g->lines[r2];
    g->lines[r2] = tmp;
  }
}

int screen_calc_line_height(struct screen *s, int width) {
  if (width <= s->w) return 1;
  return (width + s->w - 1) / s->w;
}

int scrollback_count_lines(struct screen *s) {
  int lines = 0;
  for (size_t i = 0; i < s->scrollback.lines.length; i++) {
    struct scrollback_line *l = vec_nth(&s->scrollback.lines, i);
    int height = screen_calc_line_height(s, l->length);
    assert(height >= 1);
    lines += height;
  }
  return lines;
}

// If the cursor is outside the scroll region, this should do nothing.
// Lines outside the scroll region must not be affected by this.
void screen_insert_lines(struct screen *g, int n) {
  assert(n > 0);
  assert(g->scroll_top >= 0);
  assert(g->scroll_bottom < g->h);
  if (g->cursor.line < g->scroll_top || g->cursor.line > g->scroll_bottom) return;
  screen_shuffle_rows_down(g, n, g->cursor.line, g->scroll_bottom);
}

// If the cursor is outside the scroll region, this should do nothing.
// Lines outside the scroll region must not be affected by this.
void screen_delete_lines(struct screen *g, int n) {
  assert(n > 0);
  assert(g->scroll_top >= 0);
  assert(g->scroll_bottom < g->h);
  if (g->cursor.line < g->scroll_top || g->cursor.line > g->scroll_bottom) return;
  screen_shuffle_rows_up(g, n, g->cursor.line, g->scroll_bottom);
}

void screen_newline(struct screen *g, bool carriage) {
  if (carriage) screen_carriage_return(g);
  struct screen_line *row = screen_row(g);
  row->has_newline = true;
  screen_move_or_scroll_down(g);
}

void screen_scroll_content_down(struct screen *g, int count) {
  screen_shuffle_rows_up(g, count, g->scroll_top, g->scroll_bottom);
}

void screen_move_or_scroll_up(struct screen *g) {
  if (g->cursor.line == g->scroll_top) {
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
  if (c->line == g->scroll_bottom) {
    if (g->scrollback.enabled) {
      struct screen_line *evicted = &g->lines[0];
      screen_scrollback_push(&g->scrollback, evicted);
    }
    screen_shuffle_rows_up(g, 1, g->scroll_top, g->scroll_bottom);
  } else {
    c->line = c->line + 1;
  }
  assert(c->line < g->h);
  g->cursor.wrap_pending = false;
}


void screen_restore_cursor(struct screen *g) {
  g->cursor = g->saved_cursor;
}

void screen_save_cursor(struct screen *g) {
  // TODO: This is incorrect. This should save:
  // - [x] cursor position
  // - [x] graphic rendition
  // - [ ] character set shift state
  // - [x] state of wrap flag
  // - [x] state of origin mode
  // - [ ] state of selective erase
  g->saved_cursor = g->cursor;
}

static inline void row_set_cell(struct screen_line *row, int col, struct screen_cell new_cell) {
  row->cells[col] = new_cell;
  row->eol = MAX(row->eol, col + 1);
}

bool cell_wide(struct screen_cell c) {
  return c.cp.is_wide;
}

static void screen_insert_batch_ascii_wrapless(struct screen *g, struct screen_cell_style brush, struct u8_slice run) {
  struct cursor *cur = &g->cursor;
  struct screen_cell c = {.style = brush};

  struct screen_line *row = screen_row(g);
  for (size_t i = 0; i < run.len; i++) {
    if (cur->column == screen_right(g)) {
      c.cp.value = run.content[run.len - 1];
      row->cells[cur->column] = c;
      return;
    }
    c.cp.value = run.content[i];
    row->cells[cur->column++] = c;
  }
  cur->column = MIN(cur->column, screen_right(g));
}

void screen_insert_ascii_run(struct screen *g, struct screen_cell_style brush, struct u8_slice run, bool wrap) {
  if (run.len == 0) return;

  if (!wrap) {
    /* if wrapping is disabled, we can take even more shortcuts */
    screen_insert_batch_ascii_wrapless(g, brush, run);
    return;
  }

  int column = g->cursor.column;
  struct screen_cell c = {.style = brush};
  if (g->cursor.wrap_pending) {
    screen_move_or_scroll_down(g);
    column = 0;
  }

  struct screen_line *row = screen_row(g);
  if (column) {
    struct screen_cell *prev = &row->cells[column - 1];
    if (prev->cp.is_wide) prev->cp = codepoint_space;
  }

  for (size_t i = 0; i < run.len;) {
    for (; column < g->w && i < run.len; ) {
      c.cp.value = run.content[i++];
      row->cells[column++] = c;
    }

    if (i == run.len) break;

    row->eol = MAX(row->eol, column);
    screen_move_or_scroll_down(g);
    column = 0;
    row = screen_row(g);
  }

  row->eol = MAX(row->eol, column);
  g->cursor.wrap_pending = column == g->w;
  g->cursor.column = MIN(column, screen_right(g));
}

void screen_insert(struct screen *g, struct screen_cell c, bool wrap) {
  struct cursor *cur = &g->cursor;
  struct screen_line *row = screen_row(g);

  if (g->cursor.wrap_pending) {
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

  struct screen_cell *this = &row->cells[cur->column];
  if (cur->column && cell_wide(this[-1])) {
    /* if the previous cell is a wide character, writing this cell clears it */
    /* the cleared cell keeps its current styling */
    this[-1].cp = codepoint_space;
  }

  if (cell_wide(c)) {
    if (cur->column == screen_right(g)) {
      /* a wide character cannot start on the last column */
      if (wrap) {
        cur->column = 0;
        screen_move_or_scroll_down(g);
        row = screen_row(g);
      } else {
        /* noop -- we cannot insert the character, and we cannot scroll */
        return;
      }
    }
    row_set_cell(row, cur->column++, c);
    if (cur->column <= screen_right(g)) {
      /* If this is a wide character, clear the cell following it.
       * Note that unlike when clearing the previous character,
       * we also overwrite the style here. */
      struct screen_cell next = c;
      next.cp = codepoint_space;
      this[1] = next;
      cur->column++;
    }
  } else {
    row_set_cell(row, cur->column++, c);
  }

  if (cur->column > screen_right(g)) {
    cur->wrap_pending = true;
    cur->column = screen_right(g);
  }
}

void screen_initialize(struct screen *g, int w, int h) {
  assert(!g->lines);
  assert(!g->_cells);
  g->lines = velvet_calloc(h, sizeof(*g->lines));
  g->_cells = velvet_calloc(w * h, sizeof(*g->_cells));
  g->h = h;
  g->w = w;
  g->_cells_size = w * h;
  g->_lines_size = h;
  screen_reset_scroll_region(g);

  struct screen_cell empty_cell = { .style = style_default, .cp = codepoint_space };
  for (int i = 0; i < h; i++) {
    g->lines[i] = (struct screen_line){.cells = &g->_cells[i * w]};
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
    screen_initialize(&new, w, h);
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
  struct screen_cell template = { .cp = codepoint_space, .style = g->cursor.brush };

  for (int r = from.line; r <= to.line; r++) {
    struct screen_line *row = &g->lines[r];
    int col_start = r == from.line ? from.column : 0;
    int col_end = r == to.line ? to.column : screen_right(g);
    // We subtract 1 from eol because it refers to the number of significant characters.
    // So if there is 1 significant characters, eol is at 0.
    int eol = MAX(0, row->eol - 1);
    col_end = MIN(col_end, screen_right(g));


    for (int i = col_start; i <= col_end; i++) {
      row_set_cell(row, i, template);
    }

    // If eol was in the range we just erased, update it to be at most the start of the range.
    if (eol >= col_start) {
      // Since col_start was erased, the new eol should
      // at least be the cell preceding it.
      if (eol <= col_end) {
        row->eol = MAX(0, col_start);
      }
    }
  }
}

void screen_insert_blanks_at_cursor(struct screen *g, int n) {
  struct screen_cell template = {.cp = codepoint_space, .style = g->cursor.brush};
  struct screen_line *row = screen_row(g);
  int lcol = screen_column(g);
  for (int col = screen_right(g); col >= lcol; col--) {
    int rcol = col - n;
    struct screen_cell replacement = rcol < lcol ? template : row->cells[rcol];
    row_set_cell(row, col, replacement);
  }
  if (row->eol > lcol) row->eol = MIN(row->eol + n, g->w);
}

void screen_shift_from_cursor(struct screen *g, int n) {
  if (n == 0) return;
  struct screen_cell template = { .cp = codepoint_space, .style = g->cursor.brush };
  struct screen_line *row = screen_row(g);
  for (int col = screen_column(g); col < screen_right(g); col++) {
    int rcol = col + n;
    struct screen_cell replacement = rcol > screen_right(g) ? template : row->cells[rcol];
    row_set_cell(row, col, replacement);
  }
  if (row->eol > g->cursor.column) row->eol = MAX(0, row->eol - n);
}
void screen_carriage_return(struct screen *g) {
  screen_set_cursor_column(g, 0);
}
void screen_destroy(struct screen *screen) {
  vec_destroy(&screen->scrollback.cells);
  vec_destroy(&screen->scrollback.lines);
  free(screen->_cells);
  free(screen->lines);
  screen->_cells = NULL;
  screen->lines = NULL;
}

/* copy to content from one screen to another. This is a naive resizing implementation which just re-inserts everything
 * and counts on the final screen to be accurate */
void screen_copy(struct screen *restrict dst, const struct screen *const restrict src, bool wrap) {
  bool fullscreen = !wrap;

  // Preserve the cursor position after the copying operation.
  // This gets a bit complex because it depends how the content was wrapped or truncated.
  // The easiest way is to just put the cursor wherever it is when the cursor from the source screen is encountered.
  struct cursor source_cursor = src->cursor;
  struct cursor dst_cursor = {.line = -1, .column = -1};
  for (int row = 0; row < src->h && (wrap || row < dst->h); row++) {
    struct screen_line *screen_row = &src->lines[row];

    int col = 0;
    for (; col < src->w && col < screen_row->eol; col++) {
      struct screen_cell c = screen_row->cells[col];
      screen_insert(dst, c, wrap);
      if (row == source_cursor.line && col == source_cursor.column) {
        dst_cursor = dst->cursor;
      }
    }

    // If wrapping is not enabled, pad the line with blank cells.
    // This is done to reduce resize artifacts when resizing fullscreen applications
    // with a different background color from the default terminal background
    if (fullscreen) {
      struct screen_cell c = screen_row->cells[src->w - 1];
      c.cp = codepoint_space;
      for (; col < dst->w; col++) {
        screen_insert(dst, c, false);
        if (row == source_cursor.line && col == source_cursor.column) {
          dst_cursor = dst->cursor;
        }
      }
    }
    if (screen_row->has_newline) {
      screen_newline(dst, true);
    }
    if (row == source_cursor.line && col == source_cursor.column) {
      dst_cursor = dst->cursor;
    }
  }

  if (dst_cursor.column != -1 && dst_cursor.line != -1) {
    dst->cursor = dst_cursor;
  }
}

void screen_backspace(struct screen *g) {
  screen_set_cursor_column(g, g->cursor.column - 1);
}

void screen_full_reset(struct screen *g) {
  assert(g); assert(g->lines);
  struct cursor clear = {0};
  g->cursor = clear;
  g->saved_cursor = clear;
  struct cursor start = {.column = screen_left(g), .line = screen_top(g)};
  struct cursor end = {.column = screen_right(g), .line = screen_bottom(g)};
  screen_erase_between_cursors(g, start, end);
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
