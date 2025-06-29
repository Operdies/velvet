#include "emulator.h"
#include "grid.h"
#include "utils.h"
#include <ctype.h>

#define CSI_MAX_PARAMS 16

enum csi_fsm_state {
  CSI_GROUND,
  CSI_ACCEPT,
  CSI_PARAMETER,
  CSI_LEADING,
  CSI_INTERMEDIATE,
  CSI_REJECT,
};

struct csi_param {
  uint32_t primary;
  uint8_t sub[4];
};

struct csi {
  enum csi_fsm_state state;
  struct {
    struct csi_param params[CSI_MAX_PARAMS];
    int n_params;
    uint8_t leading;
    uint8_t intermediate;
    uint8_t final;
  };
};

static void csi_handle_query(struct fsm *fsm, int n, int *params) {
  (void)fsm, (void)n, (void)params;
  TODO("Implement CSI queries");
}

static void csi_query_modifiers(struct fsm *fsm, int n, int params[n]) {
  TODO("Implement query modifiers");
  if (n == 0) {
    fsm->features.modifier_options = (struct modifier_options){0};
  } else {
    int options = params[0];
    if (options >= 0 && options <= 7) {
      fsm->features.modifier_options.options[options] = params[1];
    }
  }
}

static void csi_set_modifiers(struct fsm *fsm, int n, int params[n]) {
  (void)fsm, (void)n, (void)params;
  TODO("Implement set modifiers");
}

static char *apply_sgr(struct grid_cell *c, int n, struct csi_param *params) {
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

static bool csi_read_parameter(struct csi_param *param, const uint8_t *buffer, int *read) {
  int i = 0;
  if (buffer[i] == ';') i++;
  int num = 0;
  while (isdigit(buffer[i])) {
    num *= 10;
    num += buffer[i] - '0';
    i++;
  }
  param->primary = num;
  int n_subparameters = 0;
  int value, length, color_type;
  value = length = color_type = 0;
  bool is_custom_color = num == 38 || num == 48;
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

  char *debug2 = buffer + i - 3;
  char *debug = buffer + i;
  *read = i;
  return true;
}

static int csi_process(struct csi *c, const uint8_t *buffer, int len) {
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
      const char *debug = buffer + i;
      if (!csi_read_parameter(param, buffer + i, &read)) {
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

static bool csi_dispatch_todo(struct fsm *fsm, struct csi *csi) {
  (void)fsm, (void)csi;
  TODO("CSI %c %c %c ", csi->leading, csi->intermediate, csi->final);
  return false;
}

static bool csi_dispatch_omitted(struct fsm *fsm, struct csi *csi) {
  (void)fsm, (void)csi;
  OMITTED("CSI %c %c %c ", csi->leading, csi->intermediate, csi->final);
  return false;
}

static bool csi_dispatch_decset(struct fsm *fsm, struct csi *csi) {
  bool on = csi->final == 'h';
  bool off = csi->final == 'l';
  switch (csi->params[0].primary) {
  case 1: fsm->features.application_mode = on; break;
  case 4: TODO("Implement insert / replace mode"); break;
  case 7: fsm->features.wrapping_disabled = off; break;
  case 12: break; /* Set local echo mode -- This is safe to ignore. */
  case 20: fsm->features.auto_return = on; break;
  case 25: fsm->features.cursor_hidden = off; break;
  case 1004: fsm->features.focus_reporting = on; break;
  case 1049:
    fsm->features.alternate_screen = on;
    fsm_ensure_grid_initialized(fsm);
    break;
  case 2004: fsm->features.bracketed_paste = on; break;
  default: TODO("CSI DEC: %.*s", fsm->escape_buffer.n - 1, fsm->escape_buffer.buffer + 1); break;
  }

  return true;
}

static bool csi_dispatch_dec_final(struct fsm *fsm, struct csi *csi) {
  if (csi->final == 'h' || csi->final == 'l') {
    return csi_dispatch_decset(fsm, csi);
  }
  return csi_dispatch_todo(fsm, csi);
}

static bool csi_dispatch_dec_intermediate(struct fsm *fsm, struct csi *csi) {
  switch (csi->intermediate) {
  case 0: return csi_dispatch_dec_final(fsm, csi);
  }
  return csi_dispatch_todo(fsm, csi);
}

static bool csi_dispatch_sgr(struct fsm *fsm, struct csi *csi) {
  char *error = apply_sgr(&fsm->cell, csi->n_params, csi->params);
  if (error) {
    logmsg("Error parsing SGR: %.*s: %s", fsm->escape_buffer.n - 1, fsm->escape_buffer.buffer + 1, error);
    return false;
  }

  return true;
}

static bool csi_dispatch_cup(struct fsm *fsm, struct csi *csi) {
  int col = csi->params[1].primary ? csi->params[1].primary : 1;
  int row = csi->params[0].primary ? csi->params[0].primary : 1;
  grid_position_visual_cursor(fsm->active_grid, col - 1, row - 1);
  return true;
}
static bool csi_dispatch_ech(struct fsm *fsm, struct csi *csi) {
  int clear = csi->params[0].primary ? csi->params[0].primary : 1;
  struct raw_cursor start = fsm->active_grid->cursor;
  struct raw_cursor end = {.row = start.row, .col = start.col + clear};
  grid_erase_between_cursors(fsm->active_grid, start, end);
  return true;
}

static bool csi_dispatch_ed(struct fsm *fsm, struct csi *csi) {
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

  grid_erase_between_cursors(g, start, end);

  return true;
}

// CUU, CUD, CUF, CUB
static bool csi_dispatch_cux(struct fsm *fsm, struct csi *csi) {
  int count = csi->params[0].primary ? csi->params[0].primary : 1;
  struct grid *g = fsm->active_grid;
  switch (csi->final) {
  case 'A': grid_move_cursor(g, 0, -count); break; // up
  case 'B': grid_move_cursor(g, 0, count); break;  // down
  case 'C': grid_move_cursor(g, count, 0); break;  // right
  case 'D': grid_move_cursor(g, -count, 0); break; // left
  default: return csi_dispatch_todo(fsm, csi);
  }
  return true;
}

static bool csi_dispatch_el(struct fsm *fsm, struct csi *csi) {
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
  grid_erase_between_cursors(g, start, end);
  return true;
}
static bool csi_dispatch_il(struct fsm *fsm, struct csi *csi) {
  int count = csi->params[0].primary ? csi->params[0].primary : 1;
  grid_shift_lines(fsm->active_grid, -count);
  return true;
}

static bool csi_dispatch_dl(struct fsm *fsm, struct csi *csi) {
  int count = csi->params[0].primary ? csi->params[0].primary : 1;
  grid_shift_lines(fsm->active_grid, count);
  return true;
}

static bool csi_dispatch_dch(struct fsm *fsm, struct csi *csi) {
  int count = csi->params[0].primary ? csi->params[0].primary : 1;
  grid_shift_from_cursor(fsm->active_grid, count);
  return true;
}

static bool csi_dispatch_ich(struct fsm *fsm, struct csi *csi) {
  int count = csi->params[0].primary ? csi->params[0].primary : 1;
  grid_insert_blanks_at_cursor(fsm->active_grid, count);
  return true;
}
static bool csi_dispatch_cha(struct fsm *fsm, struct csi *csi) {
  int col = csi->params[0].primary ? csi->params[0].primary : 1;
  grid_position_cursor_column(fsm->active_grid, col - 1);
  return true;
}

static bool csi_dispatch_vpa(struct fsm *fsm, struct csi *csi) {
  int row = csi->params[0].primary ? csi->params[0].primary : 1;
  grid_position_cursor_row(fsm->active_grid, row - 1);
  return true;
}

// Scroll Region
static bool csi_dispatch_decstbm(struct fsm *fsm, struct csi *csi) {
  TODO("[dispatch] Scroll Region");
  return csi_dispatch_todo(fsm, csi);
}

static bool csi_dispatch_xtwinops(struct fsm *fsm, struct csi *csi) {
  return csi_dispatch_omitted(fsm, csi);
}

static bool csi_dispatch_final(struct fsm *fsm, struct csi *csi) {
  assert(csi->leading == 0);
  assert(csi->intermediate == 0);
  switch (csi->final) {
  case '@': return csi_dispatch_ich(fsm, csi);
  case 'A': return csi_dispatch_cux(fsm, csi);
  case 'B': return csi_dispatch_cux(fsm, csi);
  case 'C': return csi_dispatch_cux(fsm, csi);
  case 'D': return csi_dispatch_cux(fsm, csi);
  case 'G': return csi_dispatch_cha(fsm, csi);
  case 'H': return csi_dispatch_cup(fsm, csi);
  case 'f': return csi_dispatch_cup(fsm, csi);
  case 'J': return csi_dispatch_ed(fsm, csi);
  case 'K': return csi_dispatch_el(fsm, csi);
  case 'L': return csi_dispatch_il(fsm, csi);
  case 'M': return csi_dispatch_dl(fsm, csi);
  case 'P': return csi_dispatch_dch(fsm, csi);
  case 'X': return csi_dispatch_ech(fsm, csi);
  case 'd': return csi_dispatch_vpa(fsm, csi);
  case 'm': return csi_dispatch_sgr(fsm, csi);
  case 'r': return csi_dispatch_decstbm(fsm, csi);
  case 't': return csi_dispatch_xtwinops(fsm, csi);
  default: return csi_dispatch_todo(fsm, csi);
  }
}

static bool csi_dispatch_intermediate(struct fsm *fsm, struct csi *csi) {
  switch (csi->intermediate) {
  case 0: return csi_dispatch_final(fsm, csi);
  default: return csi_dispatch_todo(fsm, csi);
  }
}

/*
 * A CSI command is identified primarily by the final byte,
 * but sometimes the proper action is determined by intermediate bytes.
 * One or more intermediate bytes can appear before and after
 * the optional numeric arguments. The number of intermediate bytes is not restricted by the standard, and is
 * effectively unbounded, but in practice, there will be at most one leading intermediate byte, and at most one trailing
 * intermediate byte. This implementation rejects sequences where this is not true.
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
static bool csi_dispatch(struct fsm *fsm, struct csi *csi) {
  return csi_dispatch_leading(fsm, csi);
}

void csi_apply(struct fsm *fsm, const uint8_t *buffer, int len) {
  if (len < 3) {
    logmsg("CSI sequence shorter than 3 bytes!!");
    return;
  }

  // Strip leading escape
  buffer += 2;
  len -= 2;

  struct csi csi = {0};
  int processed = csi_process(&csi, buffer, len);
  if (csi.state != CSI_ACCEPT) {
    logmsg("Error parsing CSI: %.*s", len, buffer);
    return;
  }
  assert(processed == len);

  if (csi_dispatch(fsm, &csi)) return;

  uint8_t first = csi.leading;
  uint8_t final = csi.final;
  uint8_t penultimate = csi.intermediate;
  struct grid *g = fsm->active_grid;

  int params[CSI_MAX_PARAMS] = {0};

  // Temporary fallback to previous integer list until we get rid of the rest of this method
  for (int i = 0; i < csi.n_params; i++) {
    params[i] = csi.params[i].primary;
  }

  int count = csi.n_params;
  int param1 = count > 0 ? params[0] : 1;

  logmsg("CSI: %.*s", len, buffer);

  if (first == '>' && final == 'm') {
    csi_set_modifiers(fsm, count, params);
    return;
  }
  if (first == '?' && final == 'm') {
    csi_query_modifiers(fsm, count, params);
    return;
  }
  if (final == 'p' && penultimate == '$') {
    csi_handle_query(fsm, count, params);
    return;
  }

  switch (final) {
  case 'J': { /* Erase in display */
    int mode = params[0];
    struct raw_cursor start = g->cursor;
    struct raw_cursor end = g->cursor;
    switch (mode) {
    case 1: { /* Erase from start of screen to cursor */ start.col = grid_start(g); start.row = grid_virtual_top(g);
    } break;
    case 2: { /* Erase entire screen */
      start.col = grid_start(g);
      start.row = grid_virtual_top(g);
      end.col = grid_end(g);
      end.row = grid_virtual_bottom(g);
    } break;
    case 3: { /* erase scrollback */ return;
    } break;
    case 0:
    default: { /* erase from cursor to end of screen */ end.col = grid_end(g); end.row = grid_virtual_bottom(g);
    } break;
    }
    grid_erase_between_cursors(g, start, end);
  } break;
  case 't': {
    /* xterm extensions for saving / restoring icon / window title
     * These are used by vim so we should probably support them.
     */
    if (params[0] == 22) {
      if (params[1] == 1) { /* Save Screen Icon */
        TODO("Save Icon");
      } else if (params[1] == 2) { /* Cursor Save Title */
        TODO("Save Title");
      }
    } else if (params[0] == 23) {
      if (params[1] == 1) { /* Restore Screen Icon */
        TODO("Restore Icon");
      } else if (params[1] == 2) { /* Cursor Restore Title */
        TODO("Restore Title");
      }
    }
    TODO("extension: %.*s", fsm->escape_buffer.n - 1, fsm->escape_buffer.buffer + 1);
  } break;
  case 'K': { /* delete operations */
    int mode = params[0];
    struct raw_cursor start = g->cursor;
    struct raw_cursor end = g->cursor;
    switch (mode) {
    case 1: { /* erase from start to cursor */ start.col = grid_start(g);
    } break;
    case 2: { /* erase entire line */ start.col = grid_start(g); end.col = grid_end(g);
    } break;
    case 0:
    default: {
      // erase from cursor to end
      end.col = grid_end(g);
    } break;
    }
    grid_erase_between_cursors(g, start, end);
  } break;
  case 'X': {
    int clear = params[0] ? params[0] : 1;
    struct raw_cursor start = g->cursor;
    struct raw_cursor end = {.row = start.row, .col = start.col + clear};
    grid_erase_between_cursors(g, start, end);
  } break;
  case 'A': { // up
    grid_move_cursor(g, 0, -param1);
  } break;
  case 'B': { // down
    grid_move_cursor(g, 0, param1);
  } break;
  case 'C': { // right
    grid_move_cursor(g, param1, 0);
  } break;
  case 'D': { // left
    grid_move_cursor(g, -param1, 0);
  } break;
  case 'H': { /* cursor move to coordinate */
    int col = params[1] ? params[1] : 1;
    int row = params[0] ? params[0] : 1;
    grid_position_visual_cursor(g, col - 1, row - 1);
  } break;
  case 'P': grid_shift_from_cursor(g, param1); break;       /* delete characters */
  case '@': grid_insert_blanks_at_cursor(g, param1); break; /* Insert blank characters */
  case 'c': {
    char which = buffer[len - 2];
    enum emulator_query_type e = which == '>'   ? REQUEST_SECONDARY_DEVICE_ATTRIBUTES
                                 : which == '=' ? REQUEST_TERTIARY_DEVICE_ATTRIBUTES
                                                : REQUEST_PRIMARY_DEVICE_ATTRIBUTES;
    struct emulator_query req = {.type = e};
    vec_push(&fsm->pending_requests, &req);
  } break;
  case 'n': {
    enum emulator_query_type e = params[0];
    if (e == REQUEST_CURSOR_POSITION || e == REQUEST_STATUS_OK) {
      struct emulator_query req = {.type = e};
      vec_push(&fsm->pending_requests, &req);
    } else {
      TODO("CSI[%dn", e);
    }
  } break;
  case 'd': {
    int row = params[0] ? params[0] : 1;
    grid_position_cursor_row(g, row - 1);
  } break;
  case 'G': {
    int col = params[0] ? params[0] : 1;
    grid_position_cursor_column(g, col - 1);
  } break;
  case 'b': { /* repeat last character */
    int count = params[0] ? params[0] : 1;
    struct grid_cell repeat = fsm->cell;
    if (repeat.symbol.len == 0) repeat.symbol = utf8_blank;
    for (int i = 0; i < count; i++) {
      grid_insert(g, repeat, !fsm->features.wrapping_disabled);
    }
  } break;
  case 'L': { // shift Pn lines down
    int count = params[0] ? params[0] : 1;
    grid_shift_lines(g, -count);
  } break;
  case 'M': { // shift Pn lines up
    int count = params[0] ? params[0] : 1;
    grid_shift_lines(g, count);
  } break;
  case 'm': { /* color */
    char *error = apply_sgr(&fsm->cell, count, params);
    if (error) {
      logmsg("Error parsing SGR: %.*s: %s", fsm->escape_buffer.n - 1, fsm->escape_buffer.buffer + 1, error);
    }
  } break;
  case 'r': { /* scroll region */
    TODO("Scroll Region");
    int top = count > 0 ? params[0] : 1;
    int bottom = count > 1 ? params[1] : g->h;
    top = MAX(top, 1);
    bottom = MIN(bottom, g->h);
    if (top <= bottom) {
      grid_set_scroll_region(g, top, bottom);
    }
  } break;
  default: {
    TODO("CSI: %.*s", fsm->escape_buffer.n - 1, fsm->escape_buffer.buffer + 1);
  } break;
  }
}

/** CSI Command Reference
 * CSI Ps @
 * CSI Ps SP @
 * CSI Ps A
 * CSI Ps SP A
 * CSI Ps B
 */
