#ifndef EMULATOR_H
#define EMULATOR_H

#include <stdint.h>
#include "queries.h"
#include "text.h"
#include "grid.h"

enum fsm_state {
  fsm_ground,
  fsm_utf8,
  fsm_escape,
  fsm_csi,
  fsm_osc,
  fsm_dcs,
  fsm_pnd,
  fsm_spc,
  fsm_pct,
  fsm_charset,
};

#define MAX_ESC_SEQ_LEN 4096
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

enum charset_map {
  CHARSET_G0,
  CHARSET_G1,
  CHARSET_G2,
  CHARSET_G3,
  CHARSET_LAST,
};

enum charset {
  CHARSET_ASCII,                     // B
  CHARSET_UNITED_KINGDOM,            // A
  CHARSET_FINNISH,                   // C or 5
  CHARSET_SWEDISH,                   // H
  CHARSET_GERMAN,                    // K
  CHARSET_FRENCH_CANADIAN,           // Q
  CHARSET_FRENCH,                    // R or f
  CHARSET_ITALIAN,                   // Y
  CHARSET_SPANISH,                   // Z
  CHARSET_DUTCH,                     // 4
  CHARSET_GREEK,                     // " >
  CHARSET_TURKISH,                   // % 2
  CHARSET_PORTUGUESE,                // % 6
  CHARSET_HEBREW,                    // % =
  CHARSET_SWISS,                     // =
  CHARSET_NORDIC,                    // `, E, or 6
  CHARSET_DEC_SPECIAL,               // 0
  CHARSET_DEC_SUPPLEMENTAL,          // <
  CHARSET_USER_PREFERRED,            // <
  CHARSET_DEC_TECHNICAL,             // >
  CHARSET_DEC_HEBREW,                // " 4
  CHARSET_DEC_GREEK,                 // " ?
  CHARSET_DEC_TURKISH,               // % 0
  CHARSET_DEC_SUPPLEMENTAL_GRAPHICS, // % 5
  CHARSET_DEC_CYRILLIC,              // & 4
  CHARSET_JIS_KATAKANA,              // I
  CHARSET_JIS_ROMAN,                 // J
  CHARSET_SCS_NRCS,                  // % 3
  CHARSET_DEC_RUSSIAN,               // & 5
};

// Commented charsets are not supported and will be treated as ASCII (0)
static enum charset charset_lookup[] = {
    ['0'] = CHARSET_DEC_SPECIAL, ['B'] = CHARSET_ASCII,
    // ['2'] = CHARSET_TURKISH,
    // ['4'] = CHARSET_DUTCH,
    // ['5'] = CHARSET_FINNISH,
    // ['6'] = CHARSET_NORDIC,
    // ['<'] = CHARSET_USER_PREFERRED,
    // ['='] = CHARSET_SWISS,
    // ['>'] = CHARSET_DEC_TECHNICAL,
    // ['A'] = CHARSET_UNITED_KINGDOM,
    // ['C'] = CHARSET_FINNISH,
    // ['E'] = CHARSET_NORDIC,
    // ['H'] = CHARSET_SWEDISH,
    // ['I'] = CHARSET_JIS_KATAKANA,
    // ['J'] = CHARSET_JIS_ROMAN,
    // ['K'] = CHARSET_GERMAN,
    // ['Q'] = CHARSET_FRENCH_CANADIAN,
    // ['R'] = CHARSET_FRENCH,
    // ['Y'] = CHARSET_ITALIAN,
    // ['Z'] = CHARSET_SPANISH,
    // ['`'] = CHARSET_NORDIC,
    // ['f'] = CHARSET_FRENCH,
};

struct charset_options {
  enum charset_map active_charset;
  uint8_t charsets[CHARSET_LAST];
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
  request_buffer pending_requests;
};

void fsm_process(struct fsm *fsm, unsigned char *buf, int n);
void fsm_destroy(struct fsm *fsm);
void grid_invalidate(struct grid *g);
void fsm_ensure_grid_initialized(struct fsm *fsm);
bool cell_equals(const struct grid_cell *const a,
                 const struct grid_cell *const b);
bool cell_style_equals(const struct grid_cell_style *const a,
                       const struct grid_cell_style *const b);
bool color_equals(const struct color *const a, const struct color *const b);
void fsm_set_active_grid(struct fsm *fsm, struct grid *grid);

#endif /*  EMULATOR_H */
