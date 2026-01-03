#include "vte.h"
#include "collections.h"
#include "csi.h"
#include "osc.h"
#include "text.h"
#include "utils.h"
#include <string.h>
#include <unistd.h>

// Commented charsets are not supported and will be treated as ASCII (0)
static enum charset charset_lookup[] = {
    ['0'] = CHARSET_DEC_SPECIAL, ['B'] = CHARSET_ASCII,
    // ['2'] = CHARSET_TURKISH, ['4'] = CHARSET_DUTCH, ['5'] = CHARSET_FINNISH, ['6'] = CHARSET_NORDIC, ['<'] = CHARSET_USER_PREFERRED,
    // ['='] = CHARSET_SWISS, ['>'] = CHARSET_DEC_TECHNICAL, ['A'] = CHARSET_UNITED_KINGDOM, ['C'] = CHARSET_FINNISH, ['E'] = CHARSET_NORDIC,
    // ['H'] = CHARSET_SWEDISH, ['I'] = CHARSET_JIS_KATAKANA, ['J'] = CHARSET_JIS_ROMAN, ['K'] = CHARSET_GERMAN, ['Q'] = CHARSET_FRENCH_CANADIAN,
    // ['R'] = CHARSET_FRENCH, ['Y'] = CHARSET_ITALIAN, ['Z'] = CHARSET_SPANISH, ['`'] = CHARSET_NORDIC, ['f'] = CHARSET_FRENCH,
};


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

void vte_send_device_attributes(struct vte *vte) {
  // Advertise VT102 support (same as alacritty)
  // TODO: Figure out how to advertise exact supported features here.
  // Step 0: find good documentation.
  string_push(&vte->pending_input, u8"\x1b[?6c");
}

void vte_send_status_report(struct vte *vte, enum vte_dsr n) {
  struct screen *active = vte_get_current_screen(vte);
  switch (n) {
  case VTE_DSR_OPERATING_STATUS: {
    // no malfunction
    string_push_csi(&vte->pending_input, 0, INT_SLICE(0), "n");
  } break;
  case VTE_DSR_CURSOR_POSITION: {
      int x, y;
      struct screen *s = vte_get_current_screen(vte);
      x = s->cursor.column;
      y = s->cursor.line;
      // respect origin mode
      if (active->cursor.origin)
        y -= s->margins.top;
      if (y < 0) y = 0;
      string_push_csi(&vte->pending_input, 0, INT_SLICE(y + 1, x + 1), "R");
  } break;
  default: break;
  }
}

static void vte_dispatch_charset(struct vte *vte, uint8_t ch) {
  assert(vte->command_buffer.len > 1);
  string_push_char(&vte->command_buffer, ch);

  int len = vte->command_buffer.len;
  if (len > 3) {
    vte->state = vte_ground;
    TODO("charset specifier too long! (rejected)");
    return;
  }

  // These symbols are not terminal.
  if (ch == '"' || ch == '%' || ch == '&') {
    return;
  }

  vte->state = vte_ground;

  uint8_t designate = vte->command_buffer.content[1];
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
    vte->options.charset.charsets[index] = new_charset;
    if (new_charset != CHARSET_ASCII) {
      TODO("Implement charset %c", ch);
    }
  } else {
    // unsupported charset -- fallback to ascii
    vte->options.charset.charsets[index] = CHARSET_ASCII;
    // TODO("Implement charset %.*s", len - 1, vte->command_buffer.content + 1);
    TODO("Implement charset %c", ch);
  }
}

static void vte_dispatch_pnd(struct vte *vte, unsigned char ch) {
  // All pnd commands are single character commands
  // and can be applied immediately
  vte->state = vte_ground;
  switch (ch) {
  case '3': OMITTED("DECDHL / TOP"); break;
  case '4': OMITTED("DECDHL / BOTTOM"); break;
  case '5': OMITTED("DECSWL"); break;
  case '6': OMITTED("DECDWL"); break;
  case '8': { /* DECALN */
    struct screen_cell E = {.cp.value = 'E'};
    struct screen *g = vte_get_current_screen(vte);
    for (int rowidx = 0; rowidx < g->h; rowidx++) {
      struct screen_line *row = screen_get_line(g, rowidx);
      for (int col = 0; col < g->w; col++) {
        row->cells[col] = E;
      }
      row->eol = g->w;
    }
  } break;
  default: {
    velvet_log("Unknown ESC # command: %x", ch);
  } break;
  }
}

static void ground_esc(struct vte *vte, uint8_t ch) {
  vte->state = vte_escape;
  string_clear(&vte->command_buffer);
  string_push_char(&vte->command_buffer, ch);
}
static void ground_noop(struct vte *vte, uint8_t ch) {
  (void)vte, (void)ch;
}

static void ground_enquiry(struct vte *vte, uint8_t ch) {
  (void)vte, (void)ch;
  TODO("Enquiry");
}

static void ground_carriage_return(struct vte *vte, uint8_t ch) {
  (void)ch;
  screen_carriage_return(vte_get_current_screen(vte));
}

static void ground_backspace(struct vte *vte, uint8_t ch) {
  (void)ch;
  screen_backspace(vte_get_current_screen(vte));
}
static void ground_vtab(struct vte *vte, uint8_t ch) {
  (void)ch;
  screen_move_or_scroll_down(vte_get_current_screen(vte));
}

static void ground_tab(struct vte *vte, uint8_t ch) {
  (void)ch;
  const int tabwidth = 8;
  int x = vte_get_current_screen(vte)->cursor.column;
  int x2 = ((x / tabwidth) + 1) * tabwidth;
  int numSpaces = x2 - x;
  struct screen_cell c = { .style = vte_get_current_screen(vte)->cursor.brush, .cp = codepoint_space };
  screen_insert(vte_get_current_screen(vte), c, vte->options.auto_wrap_mode);
  for (int i = 1; i < numSpaces; i++) {
    screen_insert(vte_get_current_screen(vte), c, false);
  }
}

static void ground_bell(struct vte *vte, uint8_t ch) {
  (void)vte, (void)ch;
  write(STDOUT_FILENO, "\a", 1);
}

static void ground_newline(struct vte *vte, uint8_t ch) {
  (void)ch;
  screen_newline(vte_get_current_screen(vte), vte->options.auto_return);
}

static void ground_accept(struct vte *vte) {
  struct screen *g = vte_get_current_screen(vte);

  int len;
  struct codepoint symbol = utf8_to_codepoint(vte->pending_symbol.utf8, &len);
  struct screen_cell c = { .cp = symbol, .style = vte_get_current_screen(vte)->cursor.brush };
  screen_insert(g, c, vte->options.auto_wrap_mode);
  vte->previous_symbol = symbol;
  vte->pending_symbol = (struct utf8){0};
}

static void ground_reject(struct vte *vte) {
  struct utf8 clear = {0};
  struct utf8 copy = vte->pending_symbol;
  vte->pending_symbol = clear;
  // If we are rejecting this symbol, we should
  // Render a replacement char for this sequence (U+FFFD)
  struct screen_cell replacement = {.cp = codepoint_fffd};
  struct screen *g = vte_get_current_screen(vte);
  screen_insert(g, replacement, vte->options.auto_wrap_mode);
  uint8_t n = utf8_length(copy);
  struct u8_slice s = { .len = n - 1, .content = &copy.utf8[1] };
  if (n > 1) vte_process(vte, s);
}

static void ground_process_shift_in(struct vte *vte, uint8_t ch) {
  assert(ch == SI);
  vte->options.charset.active_charset = CHARSET_G0;
}
static void ground_process_shift_out(struct vte *vte, uint8_t ch) {
  assert(ch == SO);
  vte->options.charset.active_charset = CHARSET_G1;
}

static void DISPATCH_RI(struct vte *vte) {
  screen_move_or_scroll_up(vte_get_current_screen(vte));
}

static void DISPATCH_IND(struct vte *vte) {
  screen_move_or_scroll_down(vte_get_current_screen(vte));
}

static void DISPATCH_NEL(struct vte *vte) {
  screen_move_or_scroll_down(vte_get_current_screen(vte));
  screen_set_cursor_column(vte_get_current_screen(vte), 0);
}

static void DISPATCH_HTS(struct vte *vte)   { (void)vte; TODO("HTS"); }
static void DISPATCH_SS2(struct vte *vte)   { (void)vte; TODO("SS2"); }
static void DISPATCH_SS3(struct vte *vte)   { (void)vte; TODO("SS3"); }
static void DISPATCH_DCS(struct vte *vte)   { vte->state = vte_dcs; }
static void DISPATCH_SPA(struct vte *vte)   { (void)vte; TODO("SPA"); }
static void DISPATCH_EPA(struct vte *vte)   { (void)vte; TODO("EPA"); }
static void DISPATCH_SOS(struct vte *vte)   { (void)vte; TODO("SOS"); }
static void DISPATCH_DECID(struct vte *vte) { vte_send_device_attributes(vte); }
static void DISPATCH_CSI(struct vte *vte)   { vte->state = vte_csi; }
static void DISPATCH_ST(struct vte *vte)    { (void)vte; TODO("ST"); }
static void DISPATCH_OSC(struct vte *vte)   { vte->state = vte_osc; }
static void DISPATCH_PM(struct vte *vte)    { (void)vte; OMITTED("PM"); }
static void DISPATCH_APC(struct vte *vte)   { 
  vte->state = vte_apc;
}


static void vte_dispatch_ground(struct vte *vte, uint8_t ch) {
  // These symbols have special behavior in terms of how they affect layout
  switch (ch) {
#define CONTROL(C0, C1, cmd, _2)                                                                                      \
  case C1: DISPATCH_##cmd(vte); break;
#include "control_characters.def"
#undef CONTROL
  case NUL: ground_noop(vte, ch); break;
  case ESC: ground_esc(vte, ch); break;
  case RET: ground_carriage_return(vte, ch); break;
  case BSP: ground_backspace(vte, ch); break;
  case BELL: ground_bell(vte, ch); break;
  case SI: ground_process_shift_in(vte, ch); break;
  case SO: ground_process_shift_out(vte, ch); break;
  case DEL: ground_noop(vte, ch); break;
  case TAB: ground_tab(vte, ch); break;
  case VTAB: ground_vtab(vte, ch); break;
  case FORMFEED: ground_newline(vte, ch); break;
  case NEWLINE: ground_newline(vte, ch); break;
  case ENQ: ground_enquiry(vte, ch); break;
  default: {
    if (ch >= 0xC2 && ch <= 0xF4) { // UTF8 leading byte range
      utf8_push(&vte->pending_symbol, ch);
      vte->state = vte_utf8;
    } else if (ch <= 0x7F) { // ASCII range
      utf8_push(&vte->pending_symbol, ch);
      ground_accept(vte);
    } else { // Outside of ascii range, and not part of a valid utf8 sequence -- error symbol
      vte->pending_symbol = utf8_fffd;
      ground_accept(vte);
    }
  } break;
  }
}

static void vte_dispatch_utf8(struct vte *vte, uint8_t ch) {
  utf8_push(&vte->pending_symbol, ch);
  uint8_t len = utf8_length(vte->pending_symbol);
  uint8_t exp = utf8_expected_length(vte->pending_symbol.utf8[0]);
  assert(exp <= 4);

  if (exp <= 1) {
    // Invalid sequence
    ground_reject(vte);
    return;
  }

  // continue processing
  if (len < exp) return;

  // If this is not a continuation byte, reject the sequence
  if (len > 1 && (ch & 0xC0) != 0x80) {
    ground_reject(vte);
    return;
  }

  // If the sequence is too long, reject it
  if (len > 4) {
    ground_reject(vte);
    return;
  }

  vte->state = vte_ground;
  ground_accept(vte);
}

static void vte_full_reset(struct vte *vte) {
  vte->options = emulator_options_default;
  vte_enter_primary_screen(vte);
  screen_full_reset(&vte->primary);
}

static void vte_dispatch_escape(struct vte *vte, uint8_t ch) {
  string_push_char(&vte->command_buffer, ch);
  vte->state = vte_ground;
  switch (ch) {
  case PCT: vte->state = vte_pct; break;
  case SPC: vte->state = vte_spc; break;
  case PND: vte->state = vte_pnd; break;
  case '7': screen_save_cursor(vte_get_current_screen(vte)); break;
  case '8': screen_restore_cursor(vte_get_current_screen(vte)); break;
  case '=': vte->options.application_keypad_mode = true; break;
  case '>': vte->options.application_keypad_mode = false; break;
  case ESC: /* Literal escape */
    utf8_push(&vte->pending_symbol, ESC);
    ground_accept(vte);
    break;
  case 'c': vte_full_reset(vte); break;
  case '(': // designate G0, VT100
  case ')': // designate G1, VT100
  case '*': // designate G2, VT220
  case '+': // designate G3, VT220
  case '-': // designate G1, VT300
  case '.': // designate G2, VT300
  case '/': // designate G3, VT300
    vte->state = vte_charset;
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
#define CONTROL(C0, C1, cmd, _2)                                                                                      \
  case C0: DISPATCH_##cmd(vte); break;
#include "control_characters.def"
#undef CONTROL
  default: {
    // Unrecognized escape. Treat this char as escaped and continue parsing normally.
    TODO("Unhandled sequence ESC 0x%x", ch);
    break;
  }
  }
}

void vte_dispatch_dcs(struct vte *vte, uint8_t ch) {
  char prev = vte->command_buffer.len > 1 ? vte->command_buffer.content[vte->command_buffer.len - 1] : 0;
  string_push_char(&vte->command_buffer, ch);
  if (ch == '\\' && prev == ESC) {
    vte->state = vte_ground;
    TODO("DCS sequence: '%.*s'", (int)vte->command_buffer.len - 2, vte->command_buffer.content + 1);
  } else if (vte->command_buffer.len >= MAX_ESC_SEQ_LEN) {
    vte->state = vte_ground;
    velvet_log("Abort DCS: max length exceeded");
  }
}

static void vte_dispatch_csi(struct vte *vte, uint8_t ch) {
  string_push_char(&vte->command_buffer, ch);
  if (ch >= 0x40 && ch <= 0x7E) {
    struct csi csi = {0};
    // Strip the leading escape sequence
    struct u8_slice csi_body = string_range(&vte->command_buffer, 2, -1);
    csi_parse(&csi, csi_body);
    if (csi.state == CSI_ACCEPT) {
      csi_dispatch(vte, &csi);
    } else {
      velvet_log("Reject CSI: %.*s", csi_body.len, csi_body.content);
    }
    vte->state = vte_ground;
  } else if (vte->command_buffer.len >= MAX_ESC_SEQ_LEN) {
    vte->state = vte_ground;
    velvet_log("Abort CSI: max length exceeded");
  }
}

static void vte_dispatch_osc(struct vte *vte, uint8_t ch) {
  // https://invisible-island.net/xterm/ctlseqs/ctlseqs.html#h3-Operating-System-Commands
  // OSC commands can be terminated with either BEL or ST. Although ST is preferred, we respond to the query with the
  // same terminator as the one we received for maximum compatibility
  static const uint8_t *BEL = u8"\a";
  static const uint8_t *ST = u8"\x1b\\";
  char prev = vte->command_buffer.len > 1 ? vte->command_buffer.content[vte->command_buffer.len - 1] : 0;
  string_push_char(&vte->command_buffer, ch);
  if (ch == BELL || (ch == '\\' && prev == ESC)) {
    const uint8_t *st = ch == BELL ? BEL : ST;
    uint8_t *buffer = vte->command_buffer.content + 2;
    int len = vte->command_buffer.len - strlen((char*)st) - 2;
    struct osc osc = {0};
    osc_parse(&osc, (struct u8_slice) { .len = len, .content = buffer }, (uint8_t*)st);
    if (osc.state == OSC_ACCEPT) {
      osc_dispatch(vte, &osc);
    }
    vte->state = vte_ground;
  } else if (vte->command_buffer.len >= MAX_ESC_SEQ_LEN) {
    velvet_log("Abort OSC: max length exceeded");
    vte->state = vte_ground;
  }
}

static bool dispatch_graphics(struct vte *vte, struct u8_slice cmd) {
  return true;
}

static bool apc_dispatch(struct vte *vte, struct u8_slice cmd) {
  if (cmd.len) {
    switch (cmd.content[0]) {
    case 'G': return dispatch_graphics(vte, cmd);
    default: TODO("APC %c", cmd.content[0]);
    }
  }
  return true;
}

static void vte_dispatch_apc(struct vte *vte, uint8_t ch) {
  static const uint8_t *BEL = u8"\a";
  static const uint8_t *ST = u8"\x1b\\";

  char prev = vte->command_buffer.len > 1 ? vte->command_buffer.content[vte->command_buffer.len - 1] : 0;
  string_push_char(&vte->command_buffer, ch);
  if (ch == BELL || (ch == '\\' && prev == ESC)) {
    const uint8_t *st = ch == BELL ? BEL : ST;
    uint8_t *buffer = vte->command_buffer.content + 2;
    int len = vte->command_buffer.len - strlen((char*)st) - 2;
    struct u8_slice content = { .content = buffer, .len = len };
    apc_dispatch(vte, content);
    vte->state = vte_ground;
  } else if (vte->command_buffer.len >= MAX_ESC_SEQ_LEN) {
    velvet_log("Abort OSC: max length exceeded");
    vte->state = vte_ground;
  }
}

static void vte_dispatch_pct(struct vte *vte, uint8_t ch) {
  (void)ch;
  vte->state = vte_ground;
  TODO("Select Character Set");
}
static void vte_dispatch_spc(struct vte *vte, uint8_t ch) {
  vte->state = vte_ground;
  switch (ch) {
  case 'F': TODO("7-bit controls"); break;
  case 'G': TODO("8-bit controls"); break;
  case 'L':
  case 'M':
  case 'N': TODO("ANSI Conformance Level"); break;
  default: TODO("Unknown ESC SP command: %x", ch); break;
  }
}

static void vte_init_alternate_screen(struct vte *vte) {
  if (vte->alternate.w != vte->ws.w || vte->alternate.h != vte->ws.h) {
    struct screen new = {.w = vte->ws.w, .h =  vte->ws.h};
    screen_initialize(&new, vte->ws.w,  vte->ws.h);
    if (vte->alternate.cells) {
      screen_copy_alternate(&new, &vte->alternate);
    }
    screen_destroy(&vte->alternate);
    vte->alternate = new;
  }
}

void vte_enter_alternate_screen(struct vte *vte) {
  if (vte->options.alternate_screen) return;
  vte->options.alternate_screen = true;
  vte_init_alternate_screen(vte);
  struct screen *g = &vte->alternate;
  // TODO: when scrollback is introduced, the scrollback buffer
  // should be accessible from the alternate screen, but new lines should
  // not be appended; the `m` rows in the alternate screen should be reused.
  // Leaving the alternate screen discards the `m` rows
  struct cursor start = {.column = screen_left(g), .line = screen_top(g)};
  struct cursor end = {.column = screen_right(g), .line = screen_bottom(g)};
  screen_erase_between_cursors(g, start, end);
  vte->alternate.cursor = vte->primary.cursor;
}

static void vte_init_primary_screen(struct vte *vte) {
  if (vte->primary.w != vte->ws.w || vte->primary.h !=  vte->ws.h) {
    struct screen new = { .w = vte->ws.w, .h =  vte->ws.h, .scroll.max = vte->primary.scroll.max };
    screen_initialize(&new, vte->ws.w,  vte->ws.h);
    if (vte->primary.cells) {
      screen_copy_primary(&new, &vte->primary);
    }
    screen_destroy(&vte->primary);
    vte->primary = new;
  }
}

void vte_enter_primary_screen(struct vte *vte) {
  if (!vte->options.alternate_screen) return;
  vte->options.alternate_screen = false;
  vte_init_primary_screen(vte);
}

void vte_set_size(struct vte *vte, struct rect sz) {
  struct screen *g = vte_get_current_screen(vte);
  vte->ws = sz;
  if (g->cells == nullptr || g->w != sz.w || g->h != sz.h) {
    if (vte->options.alternate_screen) {
      vte_init_alternate_screen(vte);
    } else {
      vte_init_primary_screen(vte);
    }
  }
}

static bool is_ascii(uint8_t ch) {
  return ch >= 0x20 && ch < 0x80;
}

void vte_process(struct vte *vte, struct u8_slice str) {
  assert(vte->ws.h);
  assert(vte->ws.w);
  for (size_t i = 0; i < str.len; i++) {
    if (vte->state == vte_ground) {
      size_t j = i;
      for (; j < str.len && is_ascii(str.content[j]); j++);
      if (j > i) {
        struct screen *s = vte_get_current_screen(vte);
        struct screen_cell_style style = s->cursor.brush;
        bool wrap = vte->options.auto_wrap_mode;
        struct u8_slice run = u8_slice_range(str, i, j);
        screen_insert_ascii_run(s, style, run, wrap);
        vte->previous_symbol = (struct codepoint){.value = run.content[run.len - 1]};
        i = j;
      }
      if (j >= str.len) break;
    }
    uint8_t ch = str.content[i];
    switch (vte->state) {
    case vte_ground: vte_dispatch_ground(vte, ch); break;
    case vte_utf8: vte_dispatch_utf8(vte, ch); break;
    case vte_escape: vte_dispatch_escape(vte, ch); break;
    case vte_dcs: vte_dispatch_dcs(vte, ch); break;
    case vte_osc: vte_dispatch_osc(vte, ch); break;
    case vte_apc: vte_dispatch_apc(vte, ch); break;
    case vte_csi: vte_dispatch_csi(vte, ch); break;
    case vte_pnd: vte_dispatch_pnd(vte, ch); break;
    case vte_spc: vte_dispatch_spc(vte, ch); break;
    case vte_pct: vte_dispatch_pct(vte, ch); break;
    case vte_charset: vte_dispatch_charset(vte, ch); break;
    default: assert(!"Unreachable");
    }
  }
}

void vte_destroy(struct vte *vte) {
  screen_destroy(&vte->primary);
  screen_destroy(&vte->alternate);
  string_destroy(&vte->pending_input);
  string_destroy(&vte->command_buffer);
  string_destroy(&vte->osc.title);
  string_destroy(&vte->osc.icon);
  vec_destroy(&vte->options.kitty[0].stack);
  vec_destroy(&vte->options.kitty[1].stack);
}

struct screen *vte_get_current_screen(struct vte *vte) {
  return vte->options.alternate_screen ? &vte->alternate : &vte->primary;
}
