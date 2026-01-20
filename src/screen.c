#include "collections.h"
#include "text.h"
#include "utils.h"
#include <stdlib.h>
#include "screen.h"
#include <wchar.h>

static int num_lines(const struct screen *g) { return g->h + g->scroll.max; }

struct screen_line *screen_get_line(const struct screen *g, int n) {
  int line = (num_lines(g) + g->scroll.offset + n) % num_lines(g);
  assert(line < g->scroll.capacity);
  return &g->lines[line];
}

struct screen_line *screen_get_view_line(const struct screen *g, int n) {
  int line = (num_lines(g) + g->scroll.offset - g->scroll.view_offset + n) % num_lines(g);
  return &g->lines[line];
}

static struct screen_line *get_current_line(const struct screen *g) {
  return screen_get_line(g, g->cursor.line);
}

int screen_left(const struct screen *g) {
  (void)g;
  return 0;
}
int screen_right(const struct screen *g) {
  return g->w - 1;
}
int screen_top(const struct screen *g) {
  (void)g;
  return 0;
}
int screen_bottom(const struct screen *g) {
  return g->h - 1;
}

void screen_clear_line(struct screen *g, int n) {
  struct screen_cell clear = { .cp = codepoint_space, .style = g->cursor.brush };
  struct screen_line *row = screen_get_line(g, n);
  assert(row->cells);
  row->has_newline = false;
  row->eol = 0;
  for (int i = 0; i < g->w; i++) {
    row->cells[i] = clear;
  }
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
    struct screen_line *l1 = screen_get_line(g, r1);
    struct screen_line *l2 = screen_get_line(g, r2);
    struct screen_line tmp = *l1;
    *l1 = *l2;
    *l2 = tmp;
  }
}

int screen_calc_line_height(struct screen *s, int width) {
  if (width <= s->w) return 1;
  return (width + s->w - 1) / s->w;
}

int screen_get_scroll_offset(struct screen *s) {
  return s->scroll.view_offset;
}

void screen_set_scroll_offset(struct screen *s, int value) {
  s->scroll.view_offset = CLAMP(value, 0, s->scroll.height);
}

int screen_get_scroll_height(struct screen *s) {
  return s->scroll.height;
}

// If the cursor is outside the scroll region, this should do nothing.
// Lines outside the scroll region must not be affected by this.
void screen_insert_lines(struct screen *g, int n) {
  assert(n > 0);
  assert(g->margins.top >= 0);
  assert(g->margins.bottom < g->h);
  if (g->cursor.line < g->margins.top || g->cursor.line > g->margins.bottom) return;
  screen_shuffle_rows_down(g, n, g->cursor.line, g->margins.bottom);
}

// If the cursor is outside the scroll region, this should do nothing.
// Lines outside the scroll region must not be affected by this.
void screen_delete_lines(struct screen *g, int n) {
  assert(n > 0);
  assert(g->margins.top >= 0);
  assert(g->margins.bottom < g->h);
  if (g->cursor.line < g->margins.top || g->cursor.line > g->margins.bottom) return;
  screen_shuffle_rows_up(g, n, g->cursor.line, g->margins.bottom);
}

void screen_newline(struct screen *g, bool carriage) {
  if (carriage) screen_carriage_return(g);
  struct screen_line *row = get_current_line(g);
  row->has_newline = true;
  screen_move_or_scroll_down(g);
}

void screen_scroll_content_down(struct screen *g, int count) {
  screen_shuffle_rows_up(g, count, g->margins.top, g->margins.bottom);
}

void screen_move_or_scroll_up(struct screen *g) {
  if (g->cursor.line == g->margins.top) {
    screen_shuffle_rows_down(g, 1, g->margins.top, g->margins.bottom);
  } else {
    screen_move_cursor_relative(g, 0, -1);
  }
}

void screen_scroll_content_up(struct screen *g, int count) {
  screen_shuffle_rows_down(g, count, g->margins.top, g->margins.bottom);
}

void screen_move_or_scroll_down(struct screen *g) {
  struct cursor *c = &g->cursor;
  if (c->line == g->margins.bottom) {
    screen_shuffle_rows_up(g, 1, g->margins.top, g->margins.bottom);
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

  struct screen_line *row = get_current_line(g);
  for (size_t i = 0; i < run.len; i++) {
    if (cur->column == screen_right(g)) {
      c.cp.value = run.content[run.len - 1];
      row->cells[cur->column] = c;
      return;
    }
    c.cp.value = run.content[i];
    row->cells[cur->column++] = c;
  }
  row->eol = MAX(row->eol, cur->column);
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

  struct screen_line *row = get_current_line(g);
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
    row = get_current_line(g);
  }

  row->eol = MAX(row->eol, column);
  g->cursor.wrap_pending = column == g->w;
  g->cursor.column = MIN(column, screen_right(g));
}

void screen_insert(struct screen *g, struct screen_cell c, bool wrap) {
  struct cursor *cur = &g->cursor;
  struct screen_line *row = get_current_line(g);

  if (g->cursor.wrap_pending) {
    g->cursor.wrap_pending = false;
    assert(cur->column == screen_right(g));
    if (wrap) {
      cur->column = 0;
      screen_move_or_scroll_down(g);
      row = get_current_line(g);
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
        row = get_current_line(g);
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

static void scrollback_init(struct screen *g, int min_cap) {
  assert(g->scroll.capacity < num_lines(g));
  int initial_size = g->scroll.capacity;
  int new_size = CLAMP(g->scroll.capacity * 2, min_cap, num_lines(g));
  assert(new_size > initial_size);
  assert(new_size <= num_lines(g));
  g->cells = velvet_erealloc(g->cells, new_size * g->w, sizeof(*g->cells));
  g->lines = velvet_erealloc(g->lines, new_size, sizeof(*g->lines));
  g->scroll.capacity = new_size;

  for (int i = 0; i < initial_size; i++)
    g->lines[i].cells = &g->cells[i * g->w];

  struct screen_cell clear_cell = { .style = g->cursor.brush, .cp = codepoint_space };
  for (int i = initial_size; i < new_size; i++) {
    struct screen_line *l = &g->lines[i];
    *l = (struct screen_line){
        .has_newline = false,
        .eol = 0,
        .cells = &g->cells[i * g->w],
    };
    for (int j = 0; j < g->w; j++) {
      l->cells[j] = clear_cell;
    }
  }

}

void screen_initialize(struct screen *g, int w, int h) {
  assert(!g->cells);
  assert(!g->lines);
  g->h = h;
  g->w = w;
  /* if scrollback is enabled, pre-allocate a couple of lines */
  scrollback_init(g, g->scroll.max ? h * 2 : h);
  screen_reset_scroll_region(g);
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
    g->margins.top = top;
    g->margins.bottom = bottom;
  }
}

/* inclusive erase rectangle */
void screen_erase_rectangle(struct screen *g, int top, int left, int bottom, int right) {
  top = CLAMP(top, 0, g->h - 1);
  bottom = CLAMP(bottom, 0, g->h - 1);
  left = CLAMP(left, 0, g->w - 1);
  right = CLAMP(right, 0, g->w - 1);
  if (bottom < top || right < left) return;
  for (int r = top; r <= bottom; r++) {
    struct cursor from = { .line = r, .column = left };
    struct cursor to = { .line = r, .column = right };
    screen_erase_between_cursors(g, from, to);
  }
}

/* inclusive erase between two cursor positions */
void screen_erase_between_cursors(struct screen *g, struct cursor from, struct cursor to) {
  struct screen_cell template = { .cp = codepoint_space, .style = g->cursor.brush };

  for (int r = from.line; r <= to.line; r++) {
    struct screen_line *row = screen_get_line(g, r);
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
  struct screen_line *row = get_current_line(g);
  int lcol = g->cursor.column;
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
  struct screen_line *row = get_current_line(g);
  for (int col = g->cursor.column; col < screen_right(g); col++) {
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
  free(screen->cells);
  free(screen->lines);
  screen->cells = nullptr;
  screen->lines = nullptr;
}

void screen_copy_primary(struct screen *restrict dst, const struct screen *restrict src) {
  struct screen_line *current_src_line = get_current_line(src);
  int n_src_lines = src->h + src->scroll.height;
  int dst_column = 0;
  int dst_row = 0;

  for (int row = 0; row < n_src_lines; row++) {
    int real_row = row - src->scroll.height;
    struct screen_line *s = screen_get_line(src, real_row);
    int eol = s->eol;
    if (s->has_newline || row == (n_src_lines - 1)) {
      for (; eol && s->cells[eol - 1].cp.value == ' '; eol--);
    }
    int col = 0;
    for (; col < eol; col += s->cells[col].cp.is_wide ? 2 : 1) {
      screen_insert(dst, s->cells[col], true);
    }

    if (s == current_src_line) {
      dst_row = dst->cursor.line;
      dst_column = src->cursor.column % dst->w;
    }

    if (s->has_newline) {
      screen_newline(dst, true);
    }
  }

  int col = CLAMP(dst_column, 0, dst->w - 1);
  int row = CLAMP(dst_row, 0, dst->h - 1);
  dst->cursor = (struct cursor){.column = col, .line = row};
}

/* copy to content from one screen to another. This is a naive resizing implementation which just re-inserts everything
 * and counts on the final screen to be accurate */
void screen_copy_alternate(struct screen *restrict dst, const struct screen *const restrict src) {
  int h = MIN(src->h, dst->h);
  int w = MIN(src->w, dst->w);
  for (int row = 0; row < h ; row++) {
    const struct screen_line *s = screen_get_line(src, row);
    struct screen_line *d = screen_get_line(dst, row);
    for (int col = 0; col < MIN(w, s->eol); col++) {
      d->cells[col] = s->cells[col];
    }
    d->has_newline = s->has_newline;
    d->eol = MIN(w, s->eol);
  }
  dst->cursor = src->cursor;
  dst->cursor.column = MIN(dst->cursor.column, w - 1);
  dst->cursor.line = MIN(dst->cursor.line, h - 1);
}

void screen_backspace(struct screen *g) {
  screen_set_cursor_column(g, g->cursor.column - 1);
}

void screen_full_reset(struct screen *g) {
  assert(g); 
  assert(g->lines);
  assert(g->cells);
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

  if (top == 0 && bottom == g->h - 1) {
    g->scroll.offset += count;
    g->scroll.height = MIN(g->scroll.height + count, g->scroll.max);
    int required_capacity = CLAMP(g->scroll.offset + g->h, 0, g->scroll.max + g->h);
    if (required_capacity > g->scroll.capacity) scrollback_init(g, required_capacity);
  } else {
    for (int row = top; row <= bottom - count; row++) {
      screen_swap_rows(g, row, row + count);
    }
  }

  // clear the final `count` rows
  for (int i = 0; i < count; i++) screen_clear_line(g, bottom - i);
}
