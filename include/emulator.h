#ifndef EMULATOR_H
#define EMULATOR_H

#include <stdint.h>
#define BLANK ' '
#define MAX_ESC_SEQ_LEN 4096

struct utf8 {
  uint8_t utf8[8];
};

struct cell {
  // TODO: utf8 (multi-byte characters)
  // Right now, utf8 will probably render correctly, but not if utf8 characters are split across a line barrier
  // TODO: variable width cells (e.g. double width emojis)
  struct utf8 ch;
  uint32_t attr;
  uint8_t fg, bg;
};

struct cell_ringbuf {
  struct cell **buf;
  int n, c;
};

struct cell_line {
  int w;
  bool dirty;
  struct cell *cells;
};

struct grid {
  int w, h;
  bool *dirty;        // dirty[h]
  struct cell *cells; // cells[w*h]
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
  char buffer[MAX_ESC_SEQ_LEN];
  int n;
};

// 0-indexed grid coordinates
struct cursor_position {
  int x, y;
};

struct pane_options {
  /* if enabled, we should translate mouse events and forward them to the appropriate pane */
  bool mouse_reporting;
  /* if wrapping, we should return the cursor to the beginning of the line when
   * inserting a character which would cause an overflow. Otherwise, the cursor
   * should stay at the last column. */
  bool nowrap;
  bool auto_return;
  bool alternate_screen;
};

/* finite state machine for parsing ansi escape codes */
struct fsm {
  int w, h;
  /* the current state of the machine */
  enum fsm_state state;
  /* cell containing state relevant for new characters (fg, bg, attributes, ...)
   * Used whenever a new character is emitted */
  struct cell current;
  struct cursor_position cur;
  struct cursor_position saved_cursor;
  struct escape_sequence seq;
  struct pane_options opts;
  struct grid primary;
  struct grid alternate;
};

void fsm_process(struct fsm *fsm, unsigned char *buf, int n);
void fsm_destroy(struct fsm *fsm);

#endif /*  EMULATOR_H */
