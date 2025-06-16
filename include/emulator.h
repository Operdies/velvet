#ifndef EMULATOR_H
#define EMULATOR_H

#include <stdint.h>
#define MAX_ESC_SEQ_LEN 4096

struct utf8 {
  /* the current byte sequence */
  uint8_t utf8[8];
  /* the length of the current sequence */
  uint8_t len;
  /* the expected total length based on the leading byte */
  uint8_t expected;
};

enum cell_attributes {
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

  // Font (SGR 10–19): store in separate field if needed.
  // Optionally: 3 bits reserved for font ID (bits 16–18)

  // Reserve for future use
  ATTR_RESERVED_1 = 1u << 16,
  ATTR_RESERVED_2 = 1u << 17,
  ATTR_RESERVED_3 = 1u << 18,

  // Masks for groups
  ATTR_UNDERLINE_ANY = ATTR_UNDERLINE | ATTR_UNDERLINE_DOUBLE |
                       ATTR_UNDERLINE_CURLY | ATTR_UNDERLINE_DOTTED |
                       ATTR_UNDERLINE_DASHED,

  ATTR_BLINK_ANY = ATTR_BLINK_SLOW | ATTR_BLINK_RAPID,

  ATTR_ALL = 0x0007FFFF, // Update if you add more flags

};

void utf8_push(struct utf8 *u, uint8_t byte);
bool utf8_valid(struct utf8 *u);

struct cell {
  // TODO: utf8 (multi-byte characters)
  // Right now, utf8 will probably render correctly, but not if utf8 characters
  // are split across a line barrier
  // TODO: variable width cells (e.g. double width emojis)
  struct utf8 symbol;
  enum cell_attributes attr;
  uint8_t fg, bg;
  // Track newline locations to support rewrapping
  bool newline;
};

static const struct utf8 utf8_fffd = {.len = 3, .utf8 = {0xEF, 0xBF, 0xBD}};
static const struct utf8 utf8_blank = {.len = 1, .utf8 = {' '}};
static const struct cell empty_cell = {.symbol = utf8_blank};

struct cell_ringbuf {
  struct cell **buf;
  int n, c;
};

// 0-indexed grid coordinates
struct cursor {
  int x, y;
};

struct grid {
  int w, h;
  // line offset to the first line of the cell (e.g. cells[offset * w] is the
  // first cell in the grid)
  int offset;
  /* scroll region is local to the grid and is not persisted when the window /
   * pane is resized or alternate screen is entered */
  int scroll_top, scroll_bottom;
  bool *dirty;        // dirty[h]
  struct cell *cells; // cells[w*h]
  // The cursor can be considered as an exact pointer into the grid, ignoring
  // all offsets.
  struct cursor cursor;
  struct cursor saved_cursor;
};

enum fsm_state {
  fsm_ground,
  fsm_escape,
  // TODO:
  fsm_csi,
  fsm_osc,
  fsm_dcs,
};

struct escape_sequence {
  uint8_t buffer[MAX_ESC_SEQ_LEN];
  int n;
};

struct pane_options {
  /* if enabled, we should translate mouse events and forward them to the
   * appropriate pane */
  bool mouse_reporting;
  /* if wrapping, we should return the cursor to the beginning of the line when
   * inserting a character which would cause an overflow. Otherwise, the cursor
   * should stay at the last column. */
  bool nowrap;
  bool auto_return;
  bool alternate_screen;
  /* if enabled, pasted content should be wrapped with ESC[200~ and ESC[201~ */
  bool bracketed_paste;
  /* turned on / off by CSI ?25h/l */
  bool cursor_hidden;
};

/* finite state machine for parsing ansi escape codes */
// TODO: Reset scroll region of alternate screen when it is entered
// TODO: Reset scroll region of all grids when pane is resized
struct fsm {
  int w, h;
  /* the current state of the machine */
  enum fsm_state state;
  /* cell containing state relevant for new characters (fg, bg, attributes, ...)
   * Used whenever a new character is emitted */
  struct cell cell;
  struct escape_sequence seq;
  struct pane_options opts;
  struct grid primary;
  struct grid alternate;
  /* pointer to either primary or alternate */
  struct grid *active_grid;
};

void fsm_process(struct fsm *fsm, unsigned char *buf, int n);
void fsm_destroy(struct fsm *fsm);

#endif /*  EMULATOR_H */
