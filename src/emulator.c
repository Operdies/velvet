#include "emulator.h"
#include "collections.h"
#include "csi.h"
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

static void escape_buffer_append(struct fsm *fsm, char ch) {
  struct escape_sequence *seq = &fsm->escape_buffer;
  if (seq->n < (int)sizeof(seq->buffer)) {
    seq->buffer[seq->n] = ch;
    seq->n++;
  }
}

void fsm_set_active_grid(struct fsm *fsm, struct grid *g) {
  assert(g == &fsm->primary || g == &fsm->alternate);
  if (fsm->active_grid != g) {
    fsm->active_grid = g;
    for (int i = 0; i < g->h; i++) g->rows[i].dirty = true;
  }
  bool reflow_content = fsm->active_grid == &fsm->primary;
  grid_resize_if_needed(fsm->active_grid, fsm->w, fsm->h, reflow_content);
}

static void fsm_dispatch_charset(struct fsm *fsm, uint8_t ch) {
  assert(fsm->escape_buffer.n > 1);
  escape_buffer_append(fsm, ch);

  int len = fsm->escape_buffer.n;
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

  uint8_t designate = fsm->escape_buffer.buffer[1];
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
    fsm->features.charset_options.charsets[index] = new_charset;
    if (new_charset != CHARSET_ASCII) {
      TODO("Implement charset %c", ch);
    }
  } else {
    // unsupported charset -- fallback to ascii
    fsm->features.charset_options.charsets[index] = CHARSET_ASCII;
    // TODO("Implement charset %.*s", len - 1, fsm->escape_buffer.buffer + 1);
    TODO("Implement charset %c", ch);
  }
}

static void fsm_dispatch_pnd(struct fsm *fsm, unsigned char ch) {
  // All pnd commands are single character commands
  // and can be applied immediately
  fsm->state = fsm_ground;
  switch (ch) {
  case '3':
  case '4': TODO("DEC double height"); break;
  case '5': TODO("DEC single-width line"); break;
  case '6': TODO("DEC double-width line"); break;
  case '8': {
    struct grid_cell E = {.symbol = {.len = 1, .utf8 = {'E'}}};
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
    TODO("Unknown # command: %x", ch);
  } break;
  }
}

static void handle_osc(struct fsm *fsm, const char *st) {
  (void)st;
  TODO("OSC sequence: %.*s", fsm->escape_buffer.n - 1, fsm->escape_buffer.buffer);
}

void fsm_ensure_grid_initialized(struct fsm *fsm) {
  struct grid *g = fsm->features.alternate_screen ? &fsm->alternate : &fsm->primary;
  fsm_set_active_grid(fsm, g);
}

bool color_equals(const struct color *const a, const struct color *const b) {
  if (a->cmd != b->cmd) return false;
  switch (a->cmd) {
  case COLOR_RESET: return true;
  case COLOR_RGB: return a->color == b->color;
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
  fsm->escape_buffer.n = 0;
  escape_buffer_append(fsm, ch);
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
  grid_advance_cursor_y(fsm->active_grid);
}

static void ground_tab(struct fsm *fsm, uint8_t ch) {
  (void)ch;
  const int tabwidth = 8;
  int x = fsm->active_grid->cursor.col;
  int x2 = ((x / tabwidth) + 1) * tabwidth;
  int numSpaces = x2 - x;
  struct grid_cell c = fsm->cell;
  c.symbol = utf8_blank;
  grid_insert(fsm->active_grid, c, !fsm->features.wrapping_disabled);
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
  grid_newline(fsm->active_grid, fsm->features.auto_return);
}

static void ground_accept(struct fsm *fsm) {
  struct utf8 clear = {0};
  struct grid *g = fsm->active_grid;
  grid_insert(g, fsm->cell, !fsm->features.wrapping_disabled);
  fsm->cell.symbol = clear;
}
static void ground_reject(struct fsm *fsm) {
  struct utf8 clear = {0};
  struct utf8 copy = fsm->cell.symbol;
  fsm->cell.symbol = clear;
  // If we are rejecting this symbol, we should
  // Render a replacement char for this sequence (U+FFFD)
  struct grid_cell replacement = {.symbol = utf8_fffd};
  struct grid *g = fsm->active_grid;
  grid_insert(g, replacement, !fsm->features.wrapping_disabled);
  if (copy.len > 1) fsm_process(fsm, &copy.utf8[1], copy.len - 1);
}

static void ground_process_shift_in_out(struct fsm *fsm, uint8_t ch) {
  if (ch == SI)
    fsm->features.charset_options.active_charset = CHARSET_G0;
  else if (ch == SO)
    fsm->features.charset_options.active_charset = CHARSET_G1;
}

static void fsm_dispatch_ground(struct fsm *fsm, uint8_t ch) {
  // These symbols have special behavior in terms of how they affect layout
  switch (ch) {
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
      utf8_push(&fsm->cell.symbol, ch);
      fsm->state = fsm_utf8;
    } else if (ch <= 0x7F) { // ASCII range
      utf8_push(&fsm->cell.symbol, ch);
      ground_accept(fsm);
    } else { // Outside of ascii range, and not part of a valid utf8 sequence -- error symbol
      fsm->cell.symbol = utf8_fffd;
      ground_accept(fsm);
    }
  } break;
  }
}

static void fsm_dispatch_utf8(struct fsm *fsm, uint8_t ch) {
  utf8_push(&fsm->cell.symbol, ch);
  uint8_t len = fsm->cell.symbol.len;
  uint8_t exp = fsm->cell.symbol.expected;
  assert(exp <= 4);

  if (exp == 0) {
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
  fsm->cell.style = style_default;
  fsm->features = (struct features){0};
  fsm->active_grid = &fsm->primary;
  grid_full_reset(&fsm->primary);
}

static void fsm_dispatch_escape(struct fsm *fsm, uint8_t ch) {
  escape_buffer_append(fsm, ch);
  fsm->state = fsm_ground;
  struct grid *g = fsm->active_grid;
  switch (ch) {
  case CSI: fsm->state = fsm_csi; break;
  case OSC: fsm->state = fsm_osc; break;
  case PCT: fsm->state = fsm_pct; break;
  case SPC: fsm->state = fsm_spc; break;
  case PND: fsm->state = fsm_pnd; break;
  case DCS: fsm->state = fsm_dcs; break;
  case '6': OMITTED("Back Index"); break;
  case '7': grid_save_cursor(fsm->active_grid); break;
  case '8': grid_restore_cursor(fsm->active_grid); break;
  case '9': OMITTED("Forward Index"); break;
  case 'D': grid_advance_cursor_y(g); break;                        // Index
  case 'M': grid_advance_cursor_y_reverse(fsm->active_grid); break; // Reverse Index
  case 'E':                                                         // Next Line
    grid_advance_cursor_y(g);
    grid_carriage_return(g);
    break;
  case 'H': TODO("Horizontal Tab Set"); break;
  case 'N': TODO("Single Shift to G2"); break;
  case 'O': TODO("Single Shift to G3"); break;
  case 'V': OMITTED("Start Guarded Area"); break;
  case 'W': OMITTED("End Guarded Area"); break;
  case 'X': TODO("Start of String"); break;
  case 'Z': OMITTED("Return Terminal ID"); break;
  case '^': OMITTED("Privacy Message"); break;
  case '_': OMITTED("Application Program Command"); break;
  case '=': fsm->features.application_keypad_mode = true; break;
  case '>': fsm->features.application_keypad_mode = false; break;
  case ESC: /* Literal escape */
    utf8_push(&fsm->cell.symbol, ESC);
    ground_accept(fsm);
    break;
  case 'F': grid_move_cursor(g, 0, g->h); break;
  case 'c': fsm_full_reset(fsm); break;
  case 'l': OMITTED("Memory lock"); break;
  case 'm': OMITTED("Memory unlock"); break;
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
  default: {
    // Unrecognized escape. Treat this char as escaped and continue parsing normally.
    TODO("Unhandled sequence ESC 0x%x", ch);
    break;
  }
  }
}

void fsm_dispatch_dcs(struct fsm *fsm, uint8_t ch) {
  char prev = fsm->escape_buffer.n > 1 ? fsm->escape_buffer.buffer[fsm->escape_buffer.n - 1] : 0;
  escape_buffer_append(fsm, ch);
  if (ch == '\\' && prev == ESC) {
    fsm->state = fsm_ground;
    TODO("DCS sequence: '%.*s'", fsm->escape_buffer.n - 2, fsm->escape_buffer.buffer + 1);
  } else if (fsm->escape_buffer.n >= MAX_ESC_SEQ_LEN) {
    fsm->state = fsm_ground;
    logmsg("Abort DCS: max length exceeded");
  }
}

static void fsm_dispatch_csi(struct fsm *fsm, uint8_t ch) {
  escape_buffer_append(fsm, ch);
  if (ch >= 0x40 && ch <= 0x7E) {
    fsm->escape_buffer.buffer[fsm->escape_buffer.n] = 0;
    csi_parse_and_execute_buffer(fsm, fsm->escape_buffer.buffer, fsm->escape_buffer.n);
    fsm->state = fsm_ground;
  } else if (fsm->escape_buffer.n >= MAX_ESC_SEQ_LEN) {
    fsm->state = fsm_ground;
  }
}

static void fsm_dispatch_osc(struct fsm *fsm, uint8_t ch) {
  // https://invisible-island.net/xterm/ctlseqs/ctlseqs.html#h3-Operating-System-Commands
  // OSC commands can be terminated with either BEL or ST. Although ST is preferred, we respond to the query with the
  // same terminator as the one we received for maximum compatibility
  static const char *BEL = "\a";
  static const char *ST = "\x1b\\";
  char prev = fsm->escape_buffer.n > 1 ? fsm->escape_buffer.buffer[fsm->escape_buffer.n - 1] : 0;
  escape_buffer_append(fsm, ch);
  if (ch == BELL || (ch == '\\' && prev == ESC)) {
    handle_osc(fsm, ch == BELL ? BEL : ST);
    fsm->state = fsm_ground;
  } else if (fsm->escape_buffer.n >= MAX_ESC_SEQ_LEN) {
    fsm->state = fsm_ground;
    logmsg("Abort OSC: max length exceeded");
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
  vec_destroy(&fsm->pending_requests);
}
