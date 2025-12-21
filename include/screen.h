#ifndef SCREEN_H
#define SCREEN_H

#include "text.h"

#define screen_left(g) (0)
#define screen_right(g) (g->w - 1)
#define screen_top(g) (0)
#define screen_bottom(g) (g->h - 1)

#define PACK __attribute__((packed))

enum PACK cell_attributes {
  ATTR_NONE = 0,

  ATTR_BOLD = 1u << 0,      // SGR 1
  ATTR_FAINT = 1u << 1,     // SGR 2
  ATTR_ITALIC = 1u << 2,    // SGR 3
  ATTR_UNDERLINE = 1u << 3, // SGR 4

  ATTR_BLINK_SLOW = 1u << 4,  // SGR 5
  ATTR_BLINK_RAPID = 1u << 5, // SGR 6
  ATTR_REVERSE = 1u << 6,     // SGR 7
  ATTR_CONCEAL = 1u << 7,     // SGR 8
  ATTR_CROSSED_OUT = 1u << 8, // SGR 9

  ATTR_UNDERLINE_DOUBLE = 1u << 9,  // SGR 21 or CSI 4:2m
  ATTR_UNDERLINE_CURLY = 1u << 10,  // CSI 4:3m
  ATTR_UNDERLINE_DOTTED = 1u << 11, // CSI 4:4m
  ATTR_UNDERLINE_DASHED = 1u << 12, // CSI 4:5m

  ATTR_FRAMED = 1u << 13,    // SGR 51
  ATTR_ENCIRCLED = 1u << 14, // SGR 52
  ATTR_OVERLINED = 1u << 15, // SGR 53

  // Masks for groups
  ATTR_UNDERLINE_ANY = ATTR_UNDERLINE | ATTR_UNDERLINE_DOUBLE |
                       ATTR_UNDERLINE_CURLY | ATTR_UNDERLINE_DOTTED |
                       ATTR_UNDERLINE_DASHED,

  ATTR_BLINK_ANY = ATTR_BLINK_SLOW | ATTR_BLINK_RAPID,
};

enum PACK color_command {
  COLOR_RESET,
  COLOR_RGB,
  COLOR_TABLE,
};

struct color {
  enum color_command cmd;
  union {
    uint8_t table;
    struct {
      uint8_t r, g, b;
    };
  };
};

static const struct color color_default = {.cmd = COLOR_RESET};

struct screen_cell_style {
  enum cell_attributes attr;
  struct color fg, bg;
};

static const struct screen_cell_style style_default = {0};

struct screen_cell {
  struct screen_cell_style style;
  struct unicode_codepoint codepoint;
};

struct screen_line {
  // If set, this indicates a newline was inserted at `eol`. This is relevant when the screen is resized.
  bool has_newline;
  // Track how many characters are significant on this line. This is needed for
  // reflowing when resizing screens.
  int eol;
  struct screen_cell *cells;
};

// 0-indexed screen coordinates. This cursor points at a raw cell
struct cursor {
  int column, line;
  /* cell containing state relevant for new characters (fg, bg, attributes, ...)
   * Used whenever a new character is emitted
   */
  struct screen_cell_style brush;
  /* flag set when a character is written at the final column.
   * Cleared after wrapping, or when the cursor is moved.
   */
  bool wrap_pending;
  /* When this flag is set, absolute cursor motions are shifted by the
   * configured scroll region.
   */
  bool origin;
};

struct screen {
  int w, h, _cells_size, _lines_size;
  /* scroll region is local to the screen and is not persisted when the window /
   * pty_host is resized or alternate screen is entered */
  int scroll_top, scroll_bottom;
  struct screen_cell *_cells; // cells[w*h]
  struct screen_line *lines;   // lines[h]
  struct cursor cursor;
  struct cursor saved_cursor;
};

void screen_move_or_scroll_down(struct screen *g);
void screen_move_or_scroll_up(struct screen *g);
void screen_backspace(struct screen *g);
void screen_carriage_return(struct screen *g);
void screen_copy(struct screen *restrict dst, const struct screen *const restrict src,
               bool wrap);
void screen_destroy(struct screen *screen);
void screen_erase_between_cursors(struct screen *g, struct cursor from,
                                struct cursor to);
void screen_full_reset(struct screen *g);
void screen_initialize(struct screen *g, int w, int h);
void screen_insert(struct screen *g, struct screen_cell c, bool wrap);
void screen_insert_blanks_at_cursor(struct screen *g, int n);
void screen_move_cursor_relative(struct screen *g, int x, int y);
void screen_set_cursor_position(struct screen *g, int x, int y);
void screen_newline(struct screen *g, bool carriage);
void screen_resize_if_needed(struct screen *g, int w, int h, bool reflow);
void screen_set_cursor_row(struct screen *g, int y);
void screen_set_cursor_column(struct screen *g, int x);
void screen_reset_scroll_region(struct screen *g);
void screen_set_scroll_region(struct screen *g, int top, int bottom);
void screen_position_visual_cursor(struct screen *g, int x, int y);
void screen_shift_from_cursor(struct screen *g, int n);
void screen_insert_lines(struct screen *g, int n);
void screen_delete_lines(struct screen *g, int n);
void screen_restore_cursor(struct screen *g);
void screen_save_cursor(struct screen *g);
void screen_scroll_content_up(struct screen *g, int count);
void screen_scroll_content_down(struct screen *g, int count);
void screen_shuffle_rows_up(struct screen *g, int count, int top, int bottom);
void screen_shuffle_rows_down(struct screen *g, int count, int top, int bottom);
bool cell_wide(struct screen_cell c);

#endif /*  SCREEN_H */
