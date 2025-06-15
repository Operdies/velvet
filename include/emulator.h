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

void utf8_push(struct utf8 *u, uint8_t byte);
bool utf8_valid(struct utf8 *u);

struct cell {
  // TODO: utf8 (multi-byte characters)
  // Right now, utf8 will probably render correctly, but not if utf8 characters
  // are split across a line barrier
  // TODO: variable width cells (e.g. double width emojis)
  struct utf8 symbol;
  uint32_t attr;
  uint8_t fg, bg;
  bool wrapped;
};

static const struct utf8 utf8_fffd = {.len = 3, .utf8 = {0xEF, 0xBF, 0xBD}};
static const struct utf8 utf8_blank = {.len = 1, .utf8 = {' '}};
const static struct cell empty_cell = {.symbol = utf8_blank};

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
  // line offset to the first line of the cell (e.g. cells[offset * w] is the first cell in the grid)
  int offset;
  bool *dirty;        // dirty[h]
  struct cell *cells; // cells[w*h]
  // The cursor can be considered as an exact pointer into the grid, ignoring all offsets.
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
};

/* finite state machine for parsing ansi escape codes */
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
