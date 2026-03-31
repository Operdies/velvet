#ifndef VTE_H
#define VTE_H

#include "screen.h"
#include "text.h"
#include "collections.h"
#include <stdint.h>
#include "osc.h"

enum vte_state {
  vte_ground,
  vte_utf8,
  vte_escape,
  vte_csi,
  vte_osc,
  vte_dcs,
  vte_pnd,
  vte_spc,
  vte_pct,
  vte_apc,
  vte_charset,
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

struct charset_options {
  enum charset_map active_charset;
  uint8_t charsets[CHARSET_LAST];
};

enum cursor_style {
  CURSOR_STYLE_DEFAULT,
  CURSOR_STYLE_BLINKING_BLOCK, // blinking block
  CURSOR_STYLE_STEADY_BLOCK,
  CURSOR_STYLE_BLINKING_UNDERLINE,
  CURSOR_STYLE_STEADY_UNDERLINE,
  CURSOR_STYLE_BLINKING_BAR,
  CURSOR_STYLE_STEADY_BAR,
  CURSOR_STYLE_LAST,
};

struct cursor_options {
  /* turned on / off by CSI ?25h/l */
  bool visible;
  enum cursor_style style;
};

enum mouse_tracking {
  MOUSE_TRACKING_OFF         = 0,
  MOUSE_TRACKING_LEGACY      = 9,    // figure out if this is worth supporting.
  MOUSE_TRACKING_CLICK       = 1000, // send mouse events on click.
  MOUSE_TRACKING_CELL_MOTION = 1002, // send mouse events while mouse is held
  MOUSE_TRACKING_ALL_MOTION  = 1003, // send mouse events whenever the mouse moves
};

enum mouse_mode {
  MOUSE_MODE_DEFAULT   = 0,
  MOUSE_MODE_UTF8      = 1005,
  MOUSE_MODE_SGR       = 1006,
  MOUSE_MODE_URXVT     = 1015,
  MOUSE_MODE_SGR_PIXEL = 1016,
};

struct mouse_options {
  enum mouse_tracking tracking;
  enum mouse_mode mode;
  bool alternate_scroll_mode; // 1007
  bool urxv5;                 // 1015
  bool readline_mouse1;       // 2001
  bool readline_mouse2;       // 2002
  bool readline_mouse3;       // 2003
};

// CSI ? Ps u
enum kitty_keyboard_options {
  KITTY_KEYBOARD_NONE = 0,
  KITTY_KEYBOARD_DISAMBIGUATE_ESCAPE_CODES = 1,
  KITTY_KEYBOARD_REPORT_EVENT_TYPES = 2,
  KITTY_KEYBOARD_REPORT_ALTERNATE_KEYS = 4,
  KITTY_KEYBOARD_REPORT_ALL_KEYS_AS_ESCAPE_CODES = 8,
  KITTY_KEYBOARD_REPORT_ASSOCIATED_TEXT = 16,
};

struct kitty_options {
    enum kitty_keyboard_options options;
    enum kitty_keyboard_options ringbuf[10];
    int head;
};

struct emulator_options {
  /* if wrapping, we should return the cursor to the beginning of the line when
   * inserting a character which would cause an overflow. Otherwise, the cursor
   * should stay at the last column. */
  bool auto_wrap_mode;
  bool auto_return;
  bool alternate_screen;
  /* if enabled, pasted content should be wrapped with ESC[200~ and ESC[201~ */
  bool bracketed_paste;
  /* in application mode, arrow keys should be translated from ESC [ A-D to ESC
   * O A-D */
  bool application_mode;
  /* translate keypad keys to escaped variants so applications can distinguish
   * e.g. numpad 1 from regular 1. */
  bool application_keypad_mode;
  /* when focus reporting is enabled, send ESC [ I and ESC [ O when the velvet_window
   * receives / loses keyboard focus */
  bool focus_reporting;

  /* invert fg/bg */
  bool reverse_video;
  struct modifier_options modifiers;
  struct charset_options charset;
  struct cursor_options cursor;
  struct mouse_options mouse;
  struct kitty_options kitty[2];
};

struct tabstop_bitmap {
  uint64_t bits[16];
};

static const struct tabstop_bitmap tabstop_bitmap_default = {
    /* every 8th bit should be set, except the very first bit because column 1 is not a tab stop. */
    .bits = {
        [0] =  0x0101010101010100ULL,
        [1] =  0x0101010101010101ULL,
        [2] =  0x0101010101010101ULL,
        [3] =  0x0101010101010101ULL,
        [4] =  0x0101010101010101ULL,
        [5] =  0x0101010101010101ULL,
        [6] =  0x0101010101010101ULL,
        [7] =  0x0101010101010101ULL,
        [8] =  0x0101010101010101ULL,
        [9] =  0x0101010101010101ULL,
        [10] = 0x0101010101010101ULL,
        [11] = 0x0101010101010101ULL,
        [12] = 0x0101010101010101ULL,
        [13] = 0x0101010101010101ULL,
        [14] = 0x0101010101010101ULL,
        [15] = 0x0101010101010101ULL,
    }};

void tabstop_bitmap_set(struct tabstop_bitmap *bm, int index, bool value);
int tabstop_bitmap_prev(struct tabstop_bitmap bm, int from);
int tabstop_bitmap_next(struct tabstop_bitmap bm, int from);

/* finite state machine for parsing ansi escape codes */
struct vte {
  struct rect ws;
  /* the current state of the machine */
  enum vte_state state;
  struct emulator_options options;
  struct screen primary;
  struct screen alternate;
  struct string pending_input;
  struct string command_buffer;
  struct {
    struct {
      size_t len;
      uint8_t buffer[128];
    } title;
    struct {
      size_t len;
      uint8_t buffer[128];
    } icon;
  } osc;
  struct utf8 pending_symbol;
  struct codepoint previous_symbol;
  struct vec /* *hyperlink */ links;
  hyperlink_handle current_link;
  /* bitmap of tabstops. 64*16*x is a generous limit. It's okay if tabs break after that. */
  struct tabstop_bitmap tabstop;
};

static const struct emulator_options emulator_options_default = {
    .auto_wrap_mode = true,
    .cursor.visible = true,
};

static const struct vte vte_default = {
    .options = emulator_options_default,
    .primary = { .scroll.max = 10000, },
    .links = vec(struct osc_hyperlink*),
    .tabstop = tabstop_bitmap_default,
};

enum vte_dsr {
  VTE_DSR_OPERATING_STATUS = 5,
  VTE_DSR_CURSOR_POSITION = 6,
};


void vte_process(struct vte *vte, struct u8_slice str);
void vte_destroy(struct vte *vte);
void vte_send_device_attributes(struct vte *vte);
struct screen *vte_get_current_screen(struct vte *vte);
void vte_enter_primary_screen(struct vte *vte);
void vte_enter_alternate_screen(struct vte *vte);
void vte_set_size(struct vte *vte, struct rect sz);
void vte_send_status_report(struct vte *vte, enum vte_dsr n);

#endif /*  VTE_H */
