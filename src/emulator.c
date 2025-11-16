#include "emulator.h"
#include "collections.h"
#include "csi.h"
#include "osc.h"
#include "text.h"
#include "utils.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NUL 0
#define BELL '\a'
#define BSP '\b'
#define DEL 0x7f
#define ESC '\e'
#define FORMFEED '\f'
#define NEWLINE '\n'
#define RET '\r'
#define TAB '\t'
#define VTAB '\v'
#define CSI '['
#define DCS 'P'
#define PND '#'
#define SPC ' '
#define PCT '%'
#define OSC ']'
#define SI 0x0F
#define SO 0x0E
#define ENQ 0x5

// Unclear what this should be -- an escape sequence can contain clipboard information, which can
// in be arbitrarily large, but in practice will probably not exceed a couple of kB
// It can also contain a picture in sixel format, which can also be quite large.
#define MAX_ESC_SEQ_LEN (1 << 16)

void fsm_set_active_grid(struct fsm *fsm, struct grid *g) {
  assert(g == &fsm->primary || g == &fsm->alternate);
  bool grid_changed = false;
  if (fsm->active_grid != g) {
    grid_changed = true;
    fsm->active_grid = g;
    for (int i = 0; i < g->h; i++) g->rows[i].dirty = true;
  }
  bool reflow_content = fsm->active_grid == &fsm->primary;
  grid_resize_if_needed(fsm->active_grid, fsm->w, fsm->h, reflow_content);

  if (grid_changed && g == &fsm->alternate) {
    // TODO: when scrollback is introduced, the scrollback buffer
    // should be accessible from the alternate screen, but new lines should
    // not be appended; the `m` rows in the alternate screen should be reused.
    // Leaving the alternate screen discards the `m` rows
    g->cursor = fsm->primary.cursor;
    struct cursor start = {.col = grid_start(g), .row = grid_virtual_top(g)};
    struct cursor end = {.col = grid_end(g), .row = grid_virtual_bottom(g)};
    grid_erase_between_cursors(g, start, end);
  }
}

void fsm_send_device_attributes(struct fsm *fsm) {
  // Advertise VT102 support (same as alacritty)
  // TODO: Figure out how to advertise exact supported features here.
  // Step 0: find good documentation.
  string_push(&fsm->pending_output, u8"\x1b[?6c");
}

static void fsm_dispatch_charset(struct fsm *fsm, uint8_t ch) {
  assert(fsm->command_buffer.len > 1);
  string_push_char(&fsm->command_buffer, ch);

  int len = fsm->command_buffer.len;
  if (len > 3) {
    fsm->state = fsm_ground;
    TODO("charset specifier too long! (rejected)");
    return;
  }

  // These symbols are not terminal.
  if (ch == '"' || ch == '%' || ch == '&') {
    return;
  }

  fsm->state = fsm_ground;

  uint8_t designate = fsm->command_buffer.content[1];
  int index = 0;
  if (designate == '(') { // G0
    index = 0;
  } else if (designate == ')' || designate == '-') { // G1
    index = 1;
  } else if (designate == '*' || designate == '.') { // G2
    index = 2;
  } else if (designate == '+' || designate == '/') { // G3
    index = 3;
  } else {
    TODO("Unknown charset designator: `%c'", designate);
    return;
  }

  if (len == 3 && ch < LENGTH(charset_lookup)) {
    enum charset new_charset = charset_lookup[ch];
    fsm->options.charset.charsets[index] = new_charset;
    if (new_charset != CHARSET_ASCII) {
      TODO("Implement charset %c", ch);
    }
  } else {
    // unsupported charset -- fallback to ascii
    fsm->options.charset.charsets[index] = CHARSET_ASCII;
    // TODO("Implement charset %.*s", len - 1, fsm->command_buffer.content + 1);
    TODO("Implement charset %c", ch);
  }
}

static void fsm_dispatch_pnd(struct fsm *fsm, unsigned char ch) {
  // All pnd commands are single character commands
  // and can be applied immediately
  fsm->state = fsm_ground;
  switch (ch) {
  case '3': OMITTED("DECDHL / TOP"); break;
  case '4': OMITTED("DECDHL / BOTTOM"); break;
  case '5': OMITTED("DECSWL"); break;
  case '6': OMITTED("DECDWL"); break;
  case '8': { /* DECALN */
    struct grid_cell E = {.symbol = {.utf8 = {'E'}}};
    struct grid *g = fsm->active_grid;
    for (int rowidx = 0; rowidx < g->h; rowidx++) {
      struct grid_row *row = &g->rows[rowidx];
      for (int col = 0; col < g->w; col++) {
        row->cells[col] = E;
      }
      row->n_significant = g->w;
      row->dirty = true;
    }
  } break;
  default: {
    logmsg("Unknown ESC # command: %x", ch);
  } break;
  }
}

void fsm_ensure_grid_initialized(struct fsm *fsm) {
  struct grid *g = fsm->options.alternate_screen ? &fsm->alternate : &fsm->primary;
  fsm_set_active_grid(fsm, g);
  fsm->primary.options = &fsm->options;
  fsm->alternate.options = &fsm->options;
}

bool color_equals(const struct color *const a, const struct color *const b) {
  if (a->cmd != b->cmd) return false;
  switch (a->cmd) {
  case COLOR_RESET: return true;
  case COLOR_RGB: return a->r == b->r && a->g == b->g && a->b == b->b;
  case COLOR_TABLE: return a->table == b->table;
  }
  return false;
}

bool cell_style_equals(const struct grid_cell_style *const a, const struct grid_cell_style *const b) {
  return a->attr == b->attr && color_equals(&a->fg, &b->fg) && color_equals(&a->bg, &b->bg);
}

bool cell_equals(const struct grid_cell *const a, const struct grid_cell *const b) {
  return utf8_equals(&a->symbol, &b->symbol) && cell_style_equals(&a->style, &b->style);
}

static void ground_esc(struct fsm *fsm, uint8_t ch) {
  fsm->state = fsm_escape;
  string_clear(&fsm->command_buffer);
  string_push_char(&fsm->command_buffer, ch);
}
static void ground_noop(struct fsm *fsm, uint8_t ch) {
  (void)fsm, (void)ch;
}

static void ground_enquiry(struct fsm *fsm, uint8_t ch) {
  (void)fsm, (void)ch;
  TODO("Enquiry");
}

static void ground_carriage_return(struct fsm *fsm, uint8_t ch) {
  (void)ch;
  grid_carriage_return(fsm->active_grid);
}

static void ground_backspace(struct fsm *fsm, uint8_t ch) {
  (void)ch;
  grid_backspace(fsm->active_grid);
}
static void ground_vtab(struct fsm *fsm, uint8_t ch) {
  (void)ch;
  grid_move_or_scroll_down(fsm->active_grid);
}

static void ground_tab(struct fsm *fsm, uint8_t ch) {
  (void)ch;
  const int tabwidth = 8;
  int x = fsm->active_grid->cursor.col;
  int x2 = ((x / tabwidth) + 1) * tabwidth;
  int numSpaces = x2 - x;
  struct grid_cell c = { .style = fsm->active_grid->cursor.brush, .symbol = utf8_blank };
  grid_insert(fsm->active_grid, c, fsm->options.auto_wrap_mode);
  for (int i = 1; i < numSpaces; i++) {
    grid_insert(fsm->active_grid, c, false);
  }
}

static void ground_bell(struct fsm *fsm, uint8_t ch) {
  (void)fsm, (void)ch;
  write(STDOUT_FILENO, "\a", 1);
}

static void ground_newline(struct fsm *fsm, uint8_t ch) {
  (void)ch;
  grid_newline(fsm->active_grid, fsm->options.auto_return);
}

static void ground_accept(struct fsm *fsm) {
  struct utf8 clear = {0};
  struct grid *g = fsm->active_grid;
  struct grid_cell c = { .symbol = fsm->pending_symbol, .style = fsm->active_grid->cursor.brush };
  grid_insert(g, c, fsm->options.auto_wrap_mode);
  fsm->previous_symbol = fsm->pending_symbol;
  fsm->pending_symbol = clear;
}
static void ground_reject(struct fsm *fsm) {
  struct utf8 clear = {0};
  struct utf8 copy = fsm->pending_symbol;
  fsm->pending_symbol = clear;
  // If we are rejecting this symbol, we should
  // Render a replacement char for this sequence (U+FFFD)
  struct grid_cell replacement = {.symbol = utf8_fffd};
  struct grid *g = fsm->active_grid;
  grid_insert(g, replacement, fsm->options.auto_wrap_mode);
  uint8_t n = utf8_length(copy);
  if (n > 1) fsm_process(fsm, &copy.utf8[1], n - 1);
}

static void ground_process_shift_in_out(struct fsm *fsm, uint8_t ch) {
  if (ch == SI)
    fsm->options.charset.active_charset = CHARSET_G0;
  else if (ch == SO)
    fsm->options.charset.active_charset = CHARSET_G1;
}

static void DISPATCH_RI(struct fsm *fsm) {
  grid_move_or_scroll_up(fsm->active_grid);
}

static void DISPATCH_IND(struct fsm *fsm) {
  grid_move_or_scroll_down(fsm->active_grid);
}

static void DISPATCH_NEL(struct fsm *fsm) {
  grid_move_or_scroll_down(fsm->active_grid);
  grid_position_cursor_column(fsm->active_grid, 0);
}

static void DISPATCH_HTS(struct fsm *fsm)   { (void)fsm; TODO("HTS"); }
static void DISPATCH_SS2(struct fsm *fsm)   { (void)fsm; TODO("SS2"); }
static void DISPATCH_SS3(struct fsm *fsm)   { (void)fsm; TODO("SS3"); }
static void DISPATCH_DCS(struct fsm *fsm)   { fsm->state = fsm_dcs; }
static void DISPATCH_SPA(struct fsm *fsm)   { (void)fsm; TODO("SPA"); }
static void DISPATCH_EPA(struct fsm *fsm)   { (void)fsm; TODO("EPA"); }
static void DISPATCH_SOS(struct fsm *fsm)   { (void)fsm; TODO("SOS"); }
static void DISPATCH_DECID(struct fsm *fsm) { fsm_send_device_attributes(fsm); }
static void DISPATCH_CSI(struct fsm *fsm)   { fsm->state = fsm_csi; }
static void DISPATCH_ST(struct fsm *fsm)    { (void)fsm; TODO("ST"); }
static void DISPATCH_OSC(struct fsm *fsm)   { fsm->state = fsm_osc; }
static void DISPATCH_PM(struct fsm *fsm)    { (void)fsm; OMITTED("PM"); }
static void DISPATCH_APC(struct fsm *fsm)   { (void)fsm; OMITTED("APC"); }


static void fsm_dispatch_ground(struct fsm *fsm, uint8_t ch) {
  // These symbols have special behavior in terms of how they affect layout
  switch (ch) {
#define CONTROL(_1, seq, cmd, _2)                                                                                      \
  case seq: DISPATCH_##cmd(fsm); break;
#include "control_characters.def"
#undef CONTROL
  case NUL: ground_noop(fsm, ch); break;
  case ESC: ground_esc(fsm, ch); break;
  case RET: ground_carriage_return(fsm, ch); break;
  case BSP: ground_backspace(fsm, ch); break;
  case BELL: ground_bell(fsm, ch); break;
  case SI: ground_process_shift_in_out(fsm, ch); break;
  case SO: ground_process_shift_in_out(fsm, ch); break;
  case DEL: ground_noop(fsm, ch); break;
  case TAB: ground_tab(fsm, ch); break;
  case VTAB: ground_vtab(fsm, ch); break;
  case FORMFEED: ground_newline(fsm, ch); break;
  case NEWLINE: ground_newline(fsm, ch); break;
  case ENQ: ground_enquiry(fsm, ch); break;
  default: {
    if (ch >= 0xC2 && ch <= 0xF4) { // UTF8 leading byte range
      utf8_push(&fsm->pending_symbol, ch);
      fsm->state = fsm_utf8;
    } else if (ch <= 0x7F) { // ASCII range
      utf8_push(&fsm->pending_symbol, ch);
      ground_accept(fsm);
    } else { // Outside of ascii range, and not part of a valid utf8 sequence -- error symbol
      fsm->pending_symbol = utf8_fffd;
      ground_accept(fsm);
    }
  } break;
  }
}

static void fsm_dispatch_utf8(struct fsm *fsm, uint8_t ch) {
  utf8_push(&fsm->pending_symbol, ch);
  uint8_t len = utf8_length(fsm->pending_symbol);
  uint8_t exp = utf8_expected_length(fsm->pending_symbol.utf8[0]);
  assert(exp <= 4);

  if (exp <= 1) {
    // Invalid sequence
    ground_reject(fsm);
    return;
  }

  // continue processing
  if (len < exp) return;

  // If this is not a continuation byte, reject the sequence
  if (len > 1 && (ch & 0xC0) != 0x80) {
    ground_reject(fsm);
    return;
  }

  // If the sequence is too long, reject it
  if (len > 4) {
    ground_reject(fsm);
    return;
  }

  fsm->state = fsm_ground;
  ground_accept(fsm);
}

static void fsm_full_reset(struct fsm *fsm) {
  fsm->options = emulator_options_default;
  fsm->active_grid = &fsm->primary;
  grid_full_reset(&fsm->primary);
}

static void fsm_dispatch_escape(struct fsm *fsm, uint8_t ch) {
  string_push_char(&fsm->command_buffer, ch);
  fsm->state = fsm_ground;
  struct grid *g = fsm->active_grid;
  switch (ch) {
  case PCT: fsm->state = fsm_pct; break;
  case SPC: fsm->state = fsm_spc; break;
  case PND: fsm->state = fsm_pnd; break;
  case '7': grid_save_cursor(fsm->active_grid); break;
  case '8': grid_restore_cursor(fsm->active_grid); break;
  case '=': fsm->options.application_keypad_mode = true; break;
  case '>': fsm->options.application_keypad_mode = false; break;
  case ESC: /* Literal escape */
    utf8_push(&fsm->pending_symbol, ESC);
    ground_accept(fsm);
    break;
  case 'c': fsm_full_reset(fsm); break;
  case '(': // designate G0, VT100
  case ')': // designate G1, VT100
  case '*': // designate G2, VT220
  case '+': // designate G3, VT220
  case '-': // designate G1, VT300
  case '.': // designate G2, VT300
  case '/': // designate G3, VT300
    fsm->state = fsm_charset;
    break;
  case 'n':
  case 'o':
  case '|':
  case '}':
  case '~': TODO("Invoke Character Set"); break;
  case '6': OMITTED("Back Index"); break;
  case '9': OMITTED("Forward Index"); break;
  case 'l': OMITTED("Memory lock"); break;
  case 'm': OMITTED("Memory unlock"); break;
#define CONTROL(ch, _1, cmd, _2)                                                                                      \
  case ch: DISPATCH_##cmd(fsm); break;
#include "control_characters.def"
#undef CONTROL
  default: {
    // Unrecognized escape. Treat this char as escaped and continue parsing normally.
    TODO("Unhandled sequence ESC 0x%x", ch);
    break;
  }
  }
}

void fsm_dispatch_dcs(struct fsm *fsm, uint8_t ch) {
  char prev = fsm->command_buffer.len > 1 ? fsm->command_buffer.content[fsm->command_buffer.len - 1] : 0;
  string_push_char(&fsm->command_buffer, ch);
  if (ch == '\\' && prev == ESC) {
    fsm->state = fsm_ground;
    TODO("DCS sequence: '%.*s'", fsm->command_buffer.len - 2, fsm->command_buffer.content + 1);
  } else if (fsm->command_buffer.len >= MAX_ESC_SEQ_LEN) {
    fsm->state = fsm_ground;
    logmsg("Abort DCS: max length exceeded");
  }
}

static void fsm_dispatch_csi(struct fsm *fsm, uint8_t ch) {
  string_push_char(&fsm->command_buffer, ch);
  if (ch >= 0x40 && ch <= 0x7E) {
    fsm->command_buffer.content[fsm->command_buffer.len] = 0;
    struct csi csi = {0};
    // Strip the leading escape sequence
    uint8_t *buffer = fsm->command_buffer.content + 2;
    int len = fsm->command_buffer.len - 2;
    int parsed = csi_parse(&csi, buffer, len);
    assert(len == parsed);
    if (csi.state == CSI_ACCEPT) {
      csi_dispatch(fsm, &csi);
    }
    fsm->state = fsm_ground;
  } else if (fsm->command_buffer.len >= MAX_ESC_SEQ_LEN) {
    fsm->state = fsm_ground;
    logmsg("Abort CSI: max length exceeded");
  }
}

static void fsm_dispatch_osc(struct fsm *fsm, uint8_t ch) {
  // https://invisible-island.net/xterm/ctlseqs/ctlseqs.html#h3-Operating-System-Commands
  // OSC commands can be terminated with either BEL or ST. Although ST is preferred, we respond to the query with the
  // same terminator as the one we received for maximum compatibility
  static const uint8_t *BEL = u8"\a";
  static const uint8_t *ST = u8"\x1b\\";
  char prev = fsm->command_buffer.len > 1 ? fsm->command_buffer.content[fsm->command_buffer.len - 1] : 0;
  string_push_char(&fsm->command_buffer, ch);
  if (ch == BELL || (ch == '\\' && prev == ESC)) {
    const uint8_t *st = ch == BELL ? BEL : ST;
    uint8_t *buffer = fsm->command_buffer.content + 2;
    int len = fsm->command_buffer.len - strlen((char*)st) - 2;
    struct osc osc = {0};
    osc_parse(&osc, buffer, len, (uint8_t*)st);
    if (osc.state == OSC_ACCEPT) {
      osc_dispatch(fsm, &osc);
    }
    fsm->state = fsm_ground;
  } else if (fsm->command_buffer.len >= MAX_ESC_SEQ_LEN) {
    logmsg("Abort OSC: max length exceeded");
    fsm->state = fsm_ground;
  }
}
static void fsm_dispatch_pct(struct fsm *fsm, uint8_t ch) {
  (void)ch;
  fsm->state = fsm_ground;
  TODO("Select Character Set");
}
static void fsm_dispatch_spc(struct fsm *fsm, uint8_t ch) {
  fsm->state = fsm_ground;
  switch (ch) {
  case 'F': TODO("7-bit controls"); break;
  case 'G': TODO("8-bit controls"); break;
  case 'L':
  case 'M':
  case 'N': TODO("ANSI Conformance Level"); break;
  default: TODO("Unknown ESC SP command: %x", ch); break;
  }
}

void fsm_process(struct fsm *fsm, uint8_t *buf, int n) {
  fsm_ensure_grid_initialized(fsm);
  for (int i = 0; i < n; i++) {
    uint8_t ch = buf[i];
    switch (fsm->state) {
    case fsm_ground: fsm_dispatch_ground(fsm, ch); break;
    case fsm_utf8: fsm_dispatch_utf8(fsm, ch); break;
    case fsm_escape: fsm_dispatch_escape(fsm, ch); break;
    case fsm_dcs: fsm_dispatch_dcs(fsm, ch); break;
    case fsm_osc: fsm_dispatch_osc(fsm, ch); break;
    case fsm_csi: fsm_dispatch_csi(fsm, ch); break;
    case fsm_pnd: fsm_dispatch_pnd(fsm, ch); break;
    case fsm_spc: fsm_dispatch_spc(fsm, ch); break;
    case fsm_pct: fsm_dispatch_pct(fsm, ch); break;
    case fsm_charset: fsm_dispatch_charset(fsm, ch); break;
    default: assert(!"Unreachable");
    }
  }
}

void fsm_destroy(struct fsm *fsm) {
  grid_destroy(&fsm->primary);
  grid_destroy(&fsm->alternate);
  string_destroy(&fsm->pending_output);
  string_destroy(&fsm->command_buffer);
}
