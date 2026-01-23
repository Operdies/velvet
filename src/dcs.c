#include "dcs.h"
#include "utils.h"

#define ESC "\x1b"

static void push_color(struct vte *vte, struct color col, bool is_fg) {
  switch (col.kind) {
  case COLOR_TABLE: {
    string_push_char(&vte->pending_input, ';');
    if (col.table <= 7) {
      string_push_int(&vte->pending_input, (is_fg ? 30 : 40) + col.table);
    } else if (col.table <= 15) {
      string_push_int(&vte->pending_input, (is_fg ? 90 : 100) + col.table - 8);
    } else {
      string_push_int(&vte->pending_input, is_fg ? 38 : 48);
      string_push_char(&vte->pending_input, ';');
      string_push_int(&vte->pending_input, 5);
      string_push_char(&vte->pending_input, ';');
      string_push_int(&vte->pending_input, col.table);
    }
  } break;
  case COLOR_RGB: {
    string_push_int(&vte->pending_input, is_fg ? 38 : 48);
    string_push_char(&vte->pending_input, ';');
    string_push_int(&vte->pending_input, 8);
    string_push_char(&vte->pending_input, ';');
    string_push_int(&vte->pending_input, col.red);
    string_push_char(&vte->pending_input, ';');
    string_push_int(&vte->pending_input, col.green);
    string_push_char(&vte->pending_input, ';');
    string_push_int(&vte->pending_input, col.blue);
  } break;
  case COLOR_RESET: break; /* nothing to do */
  }
}

static void push_sgr(struct vte *vte) {
  struct screen *g = vte_get_current_screen(vte);
  uint32_t features[] = {
    [0] = ATTR_NONE,
    [1] = ATTR_BOLD,
    [2] = ATTR_FAINT,
    [3] = ATTR_ITALIC,
    [4] = ATTR_UNDERLINE,
    [5] = ATTR_BLINK_SLOW,
    [6] = ATTR_BLINK_RAPID,
    [7] = ATTR_REVERSE,
    [8] = ATTR_CONCEAL,
    [9] = ATTR_CROSSED_OUT,
  };

  uint32_t attr = g->cursor.brush.attr;
  for (size_t i = 1; i < LENGTH(features); i++) {
    uint32_t set = features[i] & attr;
    if (set) {
      string_push_char(&vte->pending_input, ';');
      string_push_int(&vte->pending_input, i);
    }
  }
  push_color(vte, g->cursor.brush.fg, true);
  push_color(vte, g->cursor.brush.bg, false);
}

static void push_unhandled(struct vte *vte, char *st) {
  string_push_cstr(&vte->pending_input, ESC "P0$r");
  string_push_cstr(&vte->pending_input, st);
}

void dcs_dispatch(struct vte *vte, struct u8_slice cmd, char *st) {
  struct screen *g = vte_get_current_screen(vte);
  if (u8_match(cmd, "$qm")) {
    /* sgr */
    string_push_cstr(&vte->pending_input, ESC "P1$r0");
    push_sgr(vte);
    string_push_char(&vte->pending_input, 'm');
    string_push_cstr(&vte->pending_input, st);
  } else if (u8_match(cmd, "$qr")) {
    /* scroll region */
    string_push_format_slow(&vte->pending_input, ESC "P1$r%d;%dr%s", g->margins.top + 1, g->margins.bottom + 1, st);
  } else if (u8_match(cmd, "$q q")) {
    /* cursor */
    string_push_format_slow(&vte->pending_input, ESC "P1$r%d q%s", vte->options.cursor.style, st);
  } else if (u8_slice_starts_with_cstr(cmd, "q")) {
    TODO("Sixel graphics");
    push_unhandled(vte, st);
  } else if (u8_slice_starts_with_cstr(cmd, "+q")) {
    /* xterm private sequence */
    push_unhandled(vte, st);
  } else {
    TODO("Unrecognized DCS sequence: '%.*s'", (int)cmd.len, cmd.content);
    push_unhandled(vte, st);
  }
}
