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
};

struct color {
  union {
    uint32_t rgb;
    struct {
      uint8_t r, g, b, basic;
    };
  };
};

/* Technically the 'default' color is ]39m / ]49m for fg/bg, but zero
 initialization simplifies things. The renderer should get the color by doing (9 + .basic) % 9
 */
static const struct color color_default = {.basic = 0};

struct grid_cell {
  // TODO: utf8 (multi-byte characters)
  // Right now, utf8 will probably render correctly, but not if utf8 characters
  // are split across a line barrier
  // TODO: variable width cells (e.g. double width emojis)
  struct utf8 symbol;
  enum cell_attributes attr;
  struct color fg, bg;

  // If enabled, the renderer should switch rendering mode when rendering this
  // cell
  bool charset_dec_special;
};

struct grid_row {
  struct grid_cell *cells;
  // Track newline locations to support rewrapping
  bool newline;
  // Indicates that this line is wrapped and should be cleared when written
  bool end_of_line;
  // Track how many characters are significant on this line. This is needed for
  // reflowing when resizing grids.
  int n_significant;
  // Track whether or not the line starting with this cell is dirty (should be
  // re-rendered)
  bool dirty;
};

static const struct utf8 utf8_fffd = {.len = 3, .utf8 = {0xEF, 0xBF, 0xBD}};
static const struct utf8 utf8_blank = {.len = 1, .utf8 = {' '}};
static const struct grid_cell empty_cell = {.symbol = utf8_blank};

// 0-indexed grid coordinates. This cursor points at a raw cell
struct raw_cursor {
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
  struct grid_cell *cells; // cells[w*h]
  struct grid_row *rows;   // rows[h]
  struct raw_cursor cursor;
  struct raw_cursor saved_cursor;
};

enum fsm_state {
  fsm_ground,
  fsm_escape,
  fsm_csi,
  fsm_osc,
  fsm_dcs,
  fsm_pnd,
  fsm_charset,
};

struct escape_sequence {
  uint8_t buffer[MAX_ESC_SEQ_LEN];
  int n;
};

struct modifier_options {
  union {
    // TODO: Implement these options?
    // https://invisible-island.net/xterm/ctlseqs/ctlseqs.html#h2-Functions-using-CSI-_-ordered-by-the-final-character_s_
    struct {
      int modifyKeyboard;
      int modifyCursorKeys;
      int modifyFunctionKeys;
      int modifyKeypadKeys;
      int modifyOtherKeys;
      int unused;
      int modifyModifierKeys;
      int modifySpecialKeys;
    };
    int options[8];
  };
};

struct charset_options {
  char g0;
  char g1;
  bool g1_active;
};

struct features {
  /* if enabled, we should translate mouse events and forward them to the
   * appropriate pane */
  bool mouse_reporting;
  /* if wrapping, we should return the cursor to the beginning of the line when
   * inserting a character which would cause an overflow. Otherwise, the cursor
   * should stay at the last column. */
  bool wrapping_disabled;
  bool auto_return;
  bool alternate_screen;
  /* if enabled, pasted content should be wrapped with ESC[200~ and ESC[201~ */
  bool bracketed_paste;
  /* turned on / off by CSI ?25h/l */
  bool cursor_hidden;
  /* in application mode, arrow keys should be translated from ESC [ A-D to ESC
   * O A-D */
  bool application_mode;
  /* when focus reporting is enabled, send ESC [ I and ESC [ O when the pane
   * receives / loses keyboard focus */
  bool focus_reporting;

  struct modifier_options modifier_options;
  bool application_keypad_mode;
  struct charset_options charset_options;
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
  struct grid_cell cell;
  struct escape_sequence escape_buffer;
  struct features features;
  struct grid primary;
  struct grid alternate;
  /* pointer to either primary or alternate */
  struct grid *active_grid;
  /* callback invoked when cursor position is requested */
  void (*on_report_cursor_position)(void *context, int row, int column);
  /* user-defined context provided to callbacks */
  void *context;
};

void fsm_process(struct fsm *fsm, unsigned char *buf, int n);
void fsm_destroy(struct fsm *fsm);
void grid_invalidate(struct grid *g);
void fsm_grid_resize(struct fsm *fsm);

#endif /*  EMULATOR_H */
