#ifndef GRID_H
#define GRID_H

#include "text.h"

#define grid_start(g) (0)
#define grid_end(g) (g->w - 1)
#define grid_top(g) (0)
#define grid_bottom(g) (g->h - 1)
#define grid_column(g) (g->cursor.col)
#define grid_line(g) (g->cursor.row)
#define grid_row(g) (&g->rows[g->cursor.row])
#define grid_virtual_top(g) (0) // (g->offset)
#define grid_virtual_bottom(g) (g->h - 1) // ((g->h + g->offset - 1) % g->h)

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

struct grid_cell_style {
  enum cell_attributes attr;
  struct color fg, bg;
};

static const struct grid_cell_style style_default = {0};

struct grid_cell {
  struct utf8 symbol;
  struct grid_cell_style style;
};

struct grid_row {
  // Track newline locations to support rewrapping
  bool newline;
  // Track whether or not the line starting with this cell is dirty (should be
  // re-rendered)
  bool dirty;
  // Track how many characters are significant on this line. This is needed for
  // reflowing when resizing grids.
  int n_significant;
  struct grid_cell *cells;
};

// 0-indexed grid coordinates. This cursor points at a raw cell
struct cursor {
  int col, row;
  /* cell containing state relevant for new characters (fg, bg, attributes, ...)
   * Used whenever a new character is emitted */
  struct grid_cell_style brush;
  /* flag set when a character is written at the final column.
   * Cleared after wrapping, or when the cursor is moved.
   * */
  bool wrap_pending;
};


struct grid {
  int w, h;
  /* scroll region is local to the grid and is not persisted when the window /
   * pane is resized or alternate screen is entered */
  int scroll_top, scroll_bottom;
  struct grid_cell *_cells; // cells[w*h]
  struct grid_row *rows;   // rows[h]
  struct cursor cursor;
  struct cursor saved_cursor;
};

void grid_advance_cursor_y(struct grid *g);
void grid_advance_cursor_y_reverse(struct grid *g);
void grid_backspace(struct grid *g);
void grid_carriage_return(struct grid *g);
void grid_copy(struct grid *restrict dst, const struct grid *const restrict src,
               bool wrap);
void grid_destroy(struct grid *grid);
void grid_erase_between_cursors(struct grid *g, struct cursor from,
                                struct cursor to);
void grid_full_reset(struct grid *g);
void grid_initialize(struct grid *g, int w, int h);
void grid_insert(struct grid *g, struct grid_cell c, bool wrap);
void grid_insert_blanks_at_cursor(struct grid *g, int n);
void grid_move_cursor(struct grid *g, int x, int y);
void grid_position_cursor(struct grid *g, int x, int y);
void grid_newline(struct grid *g, bool carriage);
void grid_resize_if_needed(struct grid *g, int w, int h, bool reflow);
void grid_position_cursor_row(struct grid *g, int y);
void grid_position_cursor_column(struct grid *g, int x);
void grid_set_scroll_region(struct grid *g, int top, int bottom);
void grid_position_visual_cursor(struct grid *g, int x, int y);
void grid_shift_from_cursor(struct grid *g, int n);
void grid_insert_lines(struct grid *g, int n);
void grid_delete_lines(struct grid *g, int n);
void grid_restore_cursor(struct grid *g);
void grid_save_cursor(struct grid *g);
#endif /*  GRID_H */
