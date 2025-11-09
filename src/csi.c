#include "csi.h"
#include "emulator.h"
#include "grid.h"
#include "utils.h"
#include <ctype.h>

static char *csi_apply_sgr_from_params(struct grid_cell *c, int n, struct csi_param *params);
static bool csi_read_subparameter(const uint8_t *buffer, uint8_t separator, int *value, int *read);
static bool csi_read_parameter(struct csi_param *param, const uint8_t *buffer, int *read, bool is_sgr);

static void csi_query_modifiers(struct fsm *fsm, int n, int params[n]) {
  TODO("Implement query modifiers");
  if (n == 0) {
    fsm->options.modifiers = (struct modifier_options){0};
  } else {
    int options = params[0];
    if (options >= 0 && options <= 7) {
      fsm->options.modifiers.options[options] = params[1];
    }
  }
}

static char *byte_names[UINT8_MAX + 1] = {
    [' '] = "SP",
    ['\a'] = "BEL",
    ['\r'] = "CR",
    ['\b'] = "BS",
    ['\f'] = "FF",
    ['\n'] = "LF",
};

static bool csi_dispatch_todo(struct fsm *fsm, struct csi *csi) {
  (void)fsm;
  char leading[] = {csi->leading, 0};
  char intermediate[] = {csi->intermediate, 0};
  char final[] = {csi->final, 0};
  TODO("CSI %2s %d %2s %2s",
       byte_names[csi->leading] ?: leading,
       csi->params[0].primary,
       byte_names[csi->intermediate] ?: intermediate,
       byte_names[csi->final] ?: final);
  return false;
}

static bool csi_dispatch_omitted(struct fsm *fsm, struct csi *csi) {
  (void)fsm, (void)csi;
  // Display these characters in a more friendly way
  char leading[] = {csi->leading, 0};
  char intermediate[] = {csi->intermediate, 0};
  char final[] = {csi->final, 0};
  OMITTED("CSI %2s %d %2s %2s",
          byte_names[csi->leading] ?: leading,
          csi->params[0].primary,
          byte_names[csi->intermediate] ?: intermediate,
          byte_names[csi->final] ?: final);
  return false;
}

/* CSI Pm h/l */
static bool csi_dispatch_set_mode(struct fsm *fsm, struct csi *csi) {
  bool on = csi->final == 'h';
  bool off = csi->final == 'l';
  assert(on || off);
  switch (csi->params[0].primary) {
  case 2: TODO("Keyboard Action Mode (KAM)"); return false;
  case 4: TODO("Insert Mode (IRM)"); return false;
  case 12: OMITTED("Send/receive (SRM)"); return false;
  case 20: fsm->options.auto_return = on; return true;
  default: TODO("Set Mode %d", csi->params[0].primary); return false;
  }
}

/* CSI ? Pm h/l */
static bool csi_dispatch_decset(struct fsm *fsm, struct csi *csi) {
  struct mouse_options *m = &fsm->options.mouse;
  bool on = csi->final == 'h';
  switch (csi->params[0].primary) {
  case 1: fsm->options.application_mode = on; break;
  case 7: fsm->options.auto_wrap_mode = on; break;
  case 12: TODO("Set Blinking Cursor"); break;
  case 25: fsm->options.cursor.visible = on; break;
  case 1004: fsm->options.focus_reporting = on; break;
  case 1049:
    fsm->options.alternate_screen = on;
    fsm_ensure_grid_initialized(fsm);
    break;
  case 2004: fsm->options.bracketed_paste = on; break;
  case 1000: m->mouse_tracking = on; break;
  case 1002: m->cell_motion = on; break;
  case 1003: m->all_motion = on; break;
  case 1006: m->sgr = on; break;
  case 1007: m->alternate_scroll_mode = on; break;
  case 1016: OMITTED("SGR Pixel Mouse Tracking"); return false;
  case 1001: OMITTED("Hilite mouse tracking"); return false;
  case 1005: OMITTED("UTF8 mouse tracking"); return false;
  case 1015: OMITTED("urxvt mouse mode"); return false;
  case 9: OMITTED("X10 mouse reporting"); return false;
  default: return csi_dispatch_todo(fsm, csi);
  }

  return true;
}

static bool csi_dispatch_dec_final(struct fsm *fsm, struct csi *csi) {
  switch (csi->final) {
  case 'h':
  case 'l': return csi_dispatch_decset(fsm, csi);
  default: return csi_dispatch_todo(fsm, csi);
  }
}

/* CSI ? ... */
static bool csi_dispatch_dec_intermediate(struct fsm *fsm, struct csi *csi) {
  switch (csi->intermediate) {
  case 0: return csi_dispatch_dec_final(fsm, csi);
  default: return csi_dispatch_todo(fsm, csi);
  }
}

/* CSI ... m */
static bool csi_dispatch_sgr(struct fsm *fsm, struct csi *csi) {
  char *error = csi_apply_sgr_from_params(&fsm->cell, csi->n_params, csi->params);
  if (error) {
    logmsg("Error parsing SGR: %.*s: %s", fsm->command_buffer.len - 1, fsm->command_buffer.content + 1, error);
    return false;
  }

  return true;
}

static bool CUP(struct fsm *fsm, struct csi *csi) {
  int col = csi->params[1].primary ? csi->params[1].primary : 1;
  int row = csi->params[0].primary ? csi->params[0].primary : 1;
  grid_position_visual_cursor(fsm->active_grid, col - 1, row - 1);
  return true;
}
static bool HVP(struct fsm *fsm, struct csi *csi) { return CUP(fsm, csi); }

// CSI Ps X
static bool csi_dispatch_ech(struct fsm *fsm, struct csi *csi) {
  int clear = csi->params[0].primary ? csi->params[0].primary : 1;
  struct raw_cursor start = fsm->active_grid->cursor;
  struct raw_cursor end = {.row = start.row, .col = start.col + clear};
  grid_erase_between_cursors(fsm->active_grid, start, end, fsm->cell.style);
  return true;
}

// CSI Ps J
static bool ED(struct fsm *fsm, struct csi *csi) {
  struct grid *g = fsm->active_grid;
  int mode = csi->params[0].primary;
  struct raw_cursor start = g->cursor;
  struct raw_cursor end = g->cursor;

  switch (mode) {
  case 1: // Erase from start of screen to cursor
    start.col = grid_start(g);
    start.row = grid_virtual_top(g);
    break;
  case 2: // Erase entire screen
    start.col = grid_start(g);
    start.row = grid_virtual_top(g);
    end.col = grid_end(g);
    end.row = grid_virtual_bottom(g);
    break;
  case 3: // erase scrollback
    return csi_dispatch_todo(fsm, csi);
  case 0:
  default: // erase from cursor to end of screen
    end.col = grid_end(g);
    end.row = grid_virtual_bottom(g);
    break;
  }

  grid_erase_between_cursors(g, start, end, fsm->cell.style);

  return true;
}

static bool CUU(struct fsm *fsm, struct csi *csi) {
  int count = csi->params[0].primary ? csi->params[0].primary : 1;
  grid_move_cursor(fsm->active_grid, 0, -count);
  return true;
}

static bool CUD(struct fsm *fsm, struct csi *csi) {
  int count = csi->params[0].primary ? csi->params[0].primary : 1;
  grid_move_cursor(fsm->active_grid, 0, count);
  return true;
}

static bool CUF(struct fsm *fsm, struct csi *csi) {
  int count = csi->params[0].primary ? csi->params[0].primary : 1;
  grid_move_cursor(fsm->active_grid, count, 0);
  return true;
}

static bool CUB(struct fsm *fsm, struct csi *csi) {
  int count = csi->params[0].primary ? csi->params[0].primary : 1;
  grid_move_cursor(fsm->active_grid, -count, 0);
  return true;
}

static bool EL(struct fsm *fsm, struct csi *csi) {
  int mode = csi->params[0].primary;
  struct grid *g = fsm->active_grid;
  struct raw_cursor start = g->cursor;
  struct raw_cursor end = g->cursor;
  switch (mode) {
  case 0: end.col = grid_end(g); break;     // erase from cursor to end
  case 1: start.col = grid_start(g); break; // erase from start to cursor
  case 2:                                   // erase entire line
    start.col = grid_start(g);
    end.col = grid_end(g);
    break;
  default: return csi_dispatch_todo(fsm, csi);
  }
  grid_erase_between_cursors(g, start, end, fsm->cell.style);
  return true;
}
static bool csi_dispatch_il(struct fsm *fsm, struct csi *csi) {
  int count = csi->params[0].primary ? csi->params[0].primary : 1;
  grid_shift_lines(fsm->active_grid, -count, fsm->cell.style);
  return true;
}

static bool csi_dispatch_dl(struct fsm *fsm, struct csi *csi) {
  int count = csi->params[0].primary ? csi->params[0].primary : 1;
  grid_shift_lines(fsm->active_grid, count, fsm->cell.style);
  return true;
}

static bool csi_dispatch_dch(struct fsm *fsm, struct csi *csi) {
  int count = csi->params[0].primary ? csi->params[0].primary : 1;
  grid_shift_from_cursor(fsm->active_grid, count, fsm->cell.style);
  return true;
}

static bool ICH(struct fsm *fsm, struct csi *csi) {
  int count = csi->params[0].primary ? csi->params[0].primary : 1;
  grid_insert_blanks_at_cursor(fsm->active_grid, count, fsm->cell.style);
  return true;
}
static bool CHA(struct fsm *fsm, struct csi *csi) {
  int col = csi->params[0].primary ? csi->params[0].primary : 1;
  grid_position_cursor_column(fsm->active_grid, col - 1);
  return true;
}

// primary device attributes
static bool csi_dispatch_da1(struct fsm *fsm, struct csi *csi) {
  switch (csi->params[0].primary) {
    case 0: {
      // Advertise VT102 support (same as alacritty)
      // TODO: Figure out how to advertise exact supported features here.
      // Step 0: find good documentation.
      string_push(&fsm->pending_output, u8"\x1b[?6c");
      return true;
    } break;
    default:
      return csi_dispatch_todo(fsm, csi);
  }
}

// Repeat the preceding graphic character Ps times (REP).
static bool REP(struct fsm *fsm, struct csi *csi) {
  struct grid *g = fsm->active_grid;
  int count = csi->params[0].primary ? csi->params[0].primary : 1;
  struct grid_cell repeat = fsm->cell;
  if (utf8_equals(&repeat.symbol, &utf8_zero)) repeat.symbol = utf8_blank;
  for (int i = 0; i < count; i++) {
    grid_insert(g, repeat, fsm->options.auto_wrap_mode);
  }
  return true;
}
static bool csi_dispatch_vpa(struct fsm *fsm, struct csi *csi) {
  int row = csi->params[0].primary ? csi->params[0].primary : 1;
  grid_position_cursor_row(fsm->active_grid, row - 1);
  return true;
}

// Scroll Region
static bool DECSTBM(struct fsm *fsm, struct csi *csi) {
  int top, bottom;
  top = csi->params[0].primary;
  bottom = csi->params[1].primary;

  if (bottom == 0) bottom = fsm->h;
  if (top > 0) top--;
  if (bottom > 0) bottom--;

  TODO("Scroll Region %d;%d", top, bottom);

  grid_set_scroll_region(fsm->active_grid, top, bottom);
  return true;
}

static bool csi_dispatch_xtwinops(struct fsm *fsm, struct csi *csi) {
  switch (csi->params[0].primary) {
  case 14: TODO("Report Text Area Size in Pixels"); return false;
  case 16: TODO("Report Cell Size in Pixels"); return false;
  case 18: TODO("Report Text Area Size in Characters"); return false;
  default:
    /* The rest of the commands in this group manipulate the window or otherwise interface with the windowing system.
     * Regardless of whether or not the underlying terminal emulator supports it, */
    return csi_dispatch_omitted(fsm, csi);
  }
}

/* cursor preceding line */
static bool CPL(struct fsm *fsm, struct csi *csi) {
  int count = csi->params[0].primary ? csi->params[0].primary : 1;
  grid_move_cursor(fsm->active_grid, 0, -count);
  grid_position_cursor_column(fsm->active_grid, 0);
  return true;
}

/* cursor next line */
static bool CNL(struct fsm *fsm, struct csi *csi) {
  int count = csi->params[0].primary ? csi->params[0].primary : 1;
  grid_move_cursor(fsm->active_grid, 0, count);
  grid_position_cursor_column(fsm->active_grid, 0);
  return true;
}

static bool csi_dispatch_final(struct fsm *fsm, struct csi *csi) {
  assert(csi->leading == 0);
  assert(csi->intermediate == 0);
  switch (csi->final) {
  case 'F': return CPL(fsm, csi);
  case 'G': return CHA(fsm, csi);
  case 'H': return CUP(fsm, csi);
  case 'J': return ED(fsm, csi);
  case 'K': return EL(fsm, csi);
  case 'L': return csi_dispatch_il(fsm, csi);
  case 'M': return csi_dispatch_dl(fsm, csi);
  case 'P': return csi_dispatch_dch(fsm, csi);
  case 'X': return csi_dispatch_ech(fsm, csi);
  case 'b': return REP(fsm, csi);
  case 'c': return csi_dispatch_da1(fsm, csi);
  case 'd': return csi_dispatch_vpa(fsm, csi);
  case 'f': return CUP(fsm, csi);
  case 'm': return csi_dispatch_sgr(fsm, csi);
  case 'r': return DECSTBM(fsm, csi);
  case 't': return csi_dispatch_xtwinops(fsm, csi);
  case 'h':
  case 'l': return csi_dispatch_set_mode(fsm, csi);
  default: return csi_dispatch_todo(fsm, csi);
  }
}

static bool csi_dispatch_decscusr(struct fsm *fsm, struct csi *csi) {
  int cursor = csi->params[0].primary;
  if (cursor >= CURSOR_STYLE_BLINKING_BLOCK && cursor < CURSOR_STYLE_LAST) {
    fsm->options.cursor.style = cursor;
  } else {
    OMITTED("Unknown cursor style %d", cursor);
  }
  return true;
}

static bool csi_dispatch_sp_final(struct fsm *fsm, struct csi *csi) {
  switch (csi->final) {
  case '@': TODO("Shift Left");
  case 'A': TODO("Shift Right");
  case 'q': return csi_dispatch_decscusr(fsm, csi);
  default: return csi_dispatch_todo(fsm, csi);
  }
  return true;
}

static bool csi_dispatch_intermediate(struct fsm *fsm, struct csi *csi) {
  switch (csi->intermediate) {
  case 0: return csi_dispatch_final(fsm, csi);
  case ' ': return csi_dispatch_sp_final(fsm, csi);
  default: return csi_dispatch_todo(fsm, csi);
  }
}

/*
 * A CSI command is identified primarily by the final byte,
 * but sometimes the proper action is determined by intermediate bytes.
 * One or more intermediate bytes can appear before and after
 * the optional numeric arguments. The number of intermediate bytes is not restricted by the standard, and is
 * effectively unbounded, but in practice, there will be at most one leading intermediate byte, and at most one
 * trailing intermediate byte. This implementation rejects sequences where this is not true.
 *
 * Our dispatching strategy is to dispatch based on the triple (leading, trailing, final).
 */
static bool csi_dispatch_leading(struct fsm *fsm, struct csi *csi) {
  switch (csi->leading) {
  case 0: return csi_dispatch_intermediate(fsm, csi);
  case '?': return csi_dispatch_dec_intermediate(fsm, csi);
  default: return csi_dispatch_todo(fsm, csi);
  }
}

static bool SL(struct fsm *fsm, struct csi *csi) { return csi_dispatch_todo(fsm, csi); }
static bool SR(struct fsm *fsm, struct csi *csi) { return csi_dispatch_todo(fsm, csi); }
static bool CHT(struct fsm *fsm, struct csi *csi) { return csi_dispatch_todo(fsm, csi); }

// "are these ancient escapes still in use??" -- Yes:
// https://github.com/xtermjs/xterm.js/issues/3651
static bool DECSED(struct fsm *fsm, struct csi *csi) { return csi_dispatch_todo(fsm, csi); }
static bool DECSEL(struct fsm *fsm, struct csi *csi) { return csi_dispatch_todo(fsm, csi); }

static char *csi_apply_sgr_from_params(struct grid_cell *c, int n, struct csi_param *params) {
  // Special case when the 0 is omitted
  if (n == 0) {
    c->style.attr = 0;
    c->style.bg = c->style.fg = color_default;
    return NULL;
  }

  for (int i = 0; i < n; i++) {
    struct csi_param *attribute = &params[i];

    if (attribute->primary == 0) {
      c->style.attr = 0;
      c->style.bg = c->style.fg = color_default;
    } else if (attribute->primary <= 9) {
      uint32_t enable[] = {
          0,
          ATTR_BOLD,
          ATTR_FAINT,
          ATTR_ITALIC,
          ATTR_UNDERLINE,
          ATTR_BLINK_SLOW,
          ATTR_BLINK_RAPID,
          ATTR_REVERSE,
          ATTR_CONCEAL,
          ATTR_CROSSED_OUT,
      };
      c->style.attr |= enable[attribute->primary];
    } else if (attribute->primary == 21) {
      c->style.attr |= ATTR_UNDERLINE_DOUBLE;
    } else if (attribute->primary >= 22 && attribute->primary <= 28) {
      uint32_t disable[] = {
          [2] = (ATTR_BOLD | ATTR_FAINT),
          [3] = ATTR_ITALIC,
          [4] = ATTR_UNDERLINE,
          [5] = ATTR_BLINK_ANY,
          /* [6] = proportional spacing */
          [7] = ATTR_REVERSE,
          [8] = ATTR_CONCEAL,
          [9] = ATTR_CROSSED_OUT,
      };
      c->style.attr &= ~disable[attribute->primary % 10];
    } else if (attribute->primary >= 30 && attribute->primary <= 49) {
      struct color *target = attribute->primary >= 40 ? &c->style.bg : &c->style.fg;
      int color = attribute->primary % 10;
      if (color == 8) {
        int type = attribute->sub[0];
        if (type == 5) { /* color from 256 color table */
          int color = attribute->sub[1];
          *target = (struct color){.table = color, .cmd = COLOR_TABLE};
        } else if (type == 2) {
          int red = attribute->sub[1];
          int green = attribute->sub[2];
          int blue = attribute->sub[3];
          *target = (struct color){.r = red, .g = green, .b = blue, .cmd = COLOR_RGB};
        }
      } else if (color == 9) { /* reset */
        *target = color_default;
      } else { /* This is a normal indexed color from 0-8 in the table */
        *target = (struct color){.table = attribute->primary % 10, .cmd = COLOR_TABLE};
      }
    } else if (attribute->primary >= 90 && attribute->primary <= 97) {
      int bright = 8 + attribute->primary - 90;
      c->style.fg = (struct color){.table = bright, .cmd = COLOR_TABLE};
    } else if (attribute->primary >= 100 && attribute->primary <= 107) {
      int bright = 8 + attribute->primary - 100;
      c->style.bg = (struct color){.table = bright, .cmd = COLOR_TABLE};
    }
  }
  return NULL;
}

#define PARAMETER(X) (isdigit((X)) || (X) == ';')
#define INTERMEDIATE(X) (((X) >= 0x20 && (X) <= 0x2F) || ((X) >= 0x3C && (X) <= 0x3F) || ((X) >= 0x5E && (X) <= 0x60))
#define ACCEPT(X) ((X) >= 0x40 && (X) <= 0x7E)

/** State machine:
 * Ground       --> Parameter
 *              --> Leading
 *              --> Accept
 * Parameter    --> Parameter
 *              --> Intermediate
 *              --> Accept
 * Leading      --> Parameter
 *              --> Intermediate
 *              --> Accept
 * Intermediate --> Accept
 */

static bool csi_read_subparameter(const uint8_t *buffer, uint8_t separator, int *value, int *read) {
  int i = 0;
  if (buffer[0] == separator) {
    i++;
    int v = 0;
    while (isdigit(buffer[i])) {
      v *= 10;
      v += buffer[i] - '0';
      i++;
    }
    *value = v;
    *read = i;
    return true;
  }
  return false;
}

static bool csi_read_parameter(struct csi_param *param, const uint8_t *buffer, int *read, bool is_sgr) {
  int i = 0;
  if (buffer[i] == ';') i++;
  int num = 0;
  while (isdigit(buffer[i])) {
    num *= 10;
    num += buffer[i] - '0';
    i++;
  }
  param->primary = num;

  // Only parse subparameters in SGR sequences
  if (is_sgr) {
    int n_subparameters = 0;
    int value, length, color_type;
    value = length = color_type = 0;
    bool is_custom_color = is_sgr && (num == 38 || num == 48);
    uint8_t separator = ':';
    int subparameter_max = LENGTH(param->sub);
    if (is_custom_color && buffer[i] == ';') {
      separator = ';';
      bool did_read = csi_read_subparameter(buffer + i, separator, &color_type, &length);
      i += length;
      if (!did_read || (color_type != 2 && color_type != 5)) {
        logmsg("Reject SGR %d: Missing color parameter", num);
        *read = i;
        return false;
      }
      param->sub[0] = color_type;
      n_subparameters++;
      subparameter_max = color_type == 2 ? 4 : 2;
    }

    while (n_subparameters < subparameter_max && csi_read_subparameter(buffer + i, separator, &value, &length)) {
      i += length;
      param->sub[n_subparameters] = value;
      n_subparameters++;
    }
    if (csi_read_subparameter(buffer + i, ':', &value, &length)) {
      logmsg("Reject CSI: Too many subparameters");
      *read = i;
      return false;
    }
  }

  *read = i;
  return true;
}

int csi_parse(struct csi *c, const uint8_t *buffer, int len) {
  if (len < 1) {
    c->state = CSI_REJECT;
    return 0;
  }
  bool is_sgr = buffer[len - 1] == 'm';
  int i = 0;
  for (; i < len;) {
    char ch = buffer[i];
    switch (c->state) {
    case CSI_GROUND: {
      c->state = PARAMETER(ch) ? CSI_PARAMETER : INTERMEDIATE(ch) ? CSI_LEADING : ACCEPT(ch) ? CSI_ACCEPT : CSI_REJECT;
    } break;
    case CSI_PARAMETER: {
      if (c->n_params >= CSI_MAX_PARAMS) {
        c->state = CSI_REJECT;
        logmsg("Reject CSI: Too many numeric parameters");
        return i;
      }

      struct csi_param *param = &c->params[c->n_params];
      c->n_params++;
      int read;
      if (!csi_read_parameter(param, buffer + i, &read, is_sgr)) {
        logmsg("Reject CSI: Error parsing parameter");
        c->state = CSI_REJECT;
        return i + read;
      }
      i += read;

      ch = buffer[i];
      c->state = PARAMETER(ch)      ? CSI_PARAMETER
                 : INTERMEDIATE(ch) ? CSI_INTERMEDIATE
                 : ACCEPT(ch)       ? CSI_ACCEPT
                                    : CSI_REJECT;
    } break;
    case CSI_LEADING: {
      char intermediate = ch;
      ch = buffer[++i];
      c->state = PARAMETER(ch)      ? CSI_PARAMETER
                 : INTERMEDIATE(ch) ? CSI_INTERMEDIATE
                 : ACCEPT(ch)       ? CSI_ACCEPT
                                    : CSI_REJECT;
      if (c->state == CSI_ACCEPT) {
        c->intermediate = intermediate;
      } else {
        c->leading = intermediate;
      }
    } break;
    case CSI_INTERMEDIATE: {
      c->intermediate = ch;
      ch = buffer[++i];
      c->state = ACCEPT(ch) ? CSI_ACCEPT : CSI_REJECT;
    } break;
    case CSI_ACCEPT: {
      // Special case for empty parameter lists
      if (c->n_params == 0) {
        c->n_params = 1;
        c->params[0].primary = 0;
      }
      c->final = ch;
      return i + 1;
    } break;
    case CSI_REJECT: {
      logmsg("Reject CSI");
      return i + 1;
    } break;
    default: assert(!"Unreachable");
    }
  }
  logmsg("Reject CSI: No accept character");
  c->state = CSI_REJECT;
  return i;
}

#undef PARAMETER
#undef ACCEPT
#undef INTERMEDIATE

bool csi_dispatch(struct fsm *fsm, struct csi *csi) {
  assert(csi->state == CSI_ACCEPT);

  // convert the three byte values into a single u32 value
#define KEY(leading, intermediate, final)                                                                              \
  ((((uint32_t)leading) << 16) | (((uint32_t)intermediate) << 8) | (((uint32_t) final)))

  // convert each CSI item from csi.def into a switch case
#define CSI(leading, intermediate, final, dispatch, _)                                                                 \
  case KEY(leading, intermediate, final): return dispatch(fsm, csi);

#define SP ' '
#define _ 0

  switch (KEY(csi->leading, csi->intermediate, csi->final)) {
#include "csi.def"
  default: return csi_dispatch_todo(fsm, csi);
  }

  return csi_dispatch_leading(fsm, csi);
}
