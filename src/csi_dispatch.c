#include "csi.h"
#include "emulator.h"
#include "utils.h"
#include <ctype.h>

/* CSI Ps @  Insert Ps (Blank) Character(s) (default = 1) (ICH). */
static bool ICH(struct fsm *fsm, struct csi *csi);
/* CSI Ps SP @ Shift left Ps columns(s) (default = 1) (SL), ECMA-48. */
static bool SL(struct fsm *fsm, struct csi *csi);
/* CSI Ps A  Cursor Up Ps Times (default = 1) (CUU). */
static bool CUU(struct fsm *fsm, struct csi *csi);
/* CSI Ps SP A Shift right Ps columns(s) (default = 1) (SR), ECMA-48. */
static bool SR(struct fsm *fsm, struct csi *csi);
/* CSI Ps B  Cursor Down Ps Times (default = 1) (CUD). */
static bool CUD(struct fsm *fsm, struct csi *csi);
/* CSI Ps C  Cursor Forward Ps Times (default = 1) (CUF). */
static bool CUF(struct fsm *fsm, struct csi *csi);
/* CSI Ps D  Cursor Backward Ps Times (default = 1) (CUB). */
static bool CUB(struct fsm *fsm, struct csi *csi);
/* CSI Ps E  Cursor Next Line Ps Times (default = 1) (CNL). */
static bool CNL(struct fsm *fsm, struct csi *csi);
/* CSI Ps F  Cursor Preceding Line Ps Times (default = 1) (CPL). */
static bool CPL(struct fsm *fsm, struct csi *csi);
/* CSI Ps G  Cursor Character Absolute  [column] (default = [row,1]) (CHA). */
static bool CHA(struct fsm *fsm, struct csi *csi);
/* CSI Ps ; Ps H Cursor Position [row;column] (default = [1,1]) (CUP). */
static bool CUP(struct fsm *fsm, struct csi *csi);
/* CSI Ps I  Cursor Forward Tabulation Ps tab stops (default = 1) (CHT). */
static bool CHT(struct fsm *fsm, struct csi *csi);
/* CSI Ps J  Erase in Display (ED), VT100. */
static bool ED(struct fsm *fsm, struct csi *csi);
/* CSI ? Ps J Erase in Display (DECSED), VT220. */
static bool DECSED(struct fsm *fsm, struct csi *csi);
/* CSI Ps K  Erase in Line (EL), VT100. */
static bool EL(struct fsm *fsm, struct csi *csi);
/* CSI ? Ps K Erase in Line (DECSEL), VT220. */
static bool DECSEL(struct fsm *fsm, struct csi *csi);
/* CSI Ps L  Insert Ps Line(s) (default = 1) (IL). */
static bool IL(struct fsm *fsm, struct csi *csi);
/* CSI Ps M  Delete Ps Line(s) (default = 1) (DL). */
static bool DL(struct fsm *fsm, struct csi *csi);
/* CSI Ps P  Delete Ps Character(s) (default = 1) (DCH). */
static bool DCH(struct fsm *fsm, struct csi *csi);
/* CSI Ps S  Scroll up Ps lines (default = 1) (SU), VT420, ECMA-48. */
static bool SU(struct fsm *fsm, struct csi *csi);
/* CSI Ps T  Scroll down Ps lines (default = 1) (SD), VT420. */
/* CSI Ps ^  Scroll down Ps lines (default = 1) (SD), ECMA-48.
          This was a publication error in the original ECMA-48 5th
          edition (1991) corrected in 2003. */
static bool SD(struct fsm *fsm, struct csi *csi);
/* CSI ? 5 W Reset tab stops to start with column 9, every 8 columns (DECST8C), VT510. */
static bool DECST8C(struct fsm *fsm, struct csi *csi);
/* CSI Ps X  Erase Ps Character(s) (default = 1) (ECH). */
static bool ECH(struct fsm *fsm, struct csi *csi);
/* CSI Ps Z  Cursor Backward Tabulation Ps tab stops (default = 1) (CBT). */
static bool CBT(struct fsm *fsm, struct csi *csi);
/* CSI Ps ^  Scroll down Ps lines (default = 1) (SD), ECMA-48. */
static bool SD(struct fsm *fsm, struct csi *csi);
/* CSI Ps `  Character Position Absolute  [column] (default = [row,1]) (HPA). */
static bool HPA(struct fsm *fsm, struct csi *csi);
/* CSI Ps a  Character Position Relative  [columns] (default = [row,col+1]) (HPR). */
static bool HPR(struct fsm *fsm, struct csi *csi);
/* CSI Ps b  Repeat the preceding graphic character Ps times (REP). */
static bool REP(struct fsm *fsm, struct csi *csi);
/* CSI Ps c  Send Device Attributes (Primary DA). */
static bool DA_PRIMARY(struct fsm *fsm, struct csi *csi);
/* CSI = Ps c Send Device Attributes (Tertiary DA). */
static bool DA_TERTIARY(struct fsm *fsm, struct csi *csi);
/* CSI > Ps c Send Device Attributes (Secondary DA). */
static bool DA_SECONDARY(struct fsm *fsm, struct csi *csi);
/* CSI Ps d  Line Position Absolute  [row] (default = [1,column]) (VPA). */
static bool VPA(struct fsm *fsm, struct csi *csi);
/* CSI Ps e  Line Position Relative  [rows] (default = [row+1,column]) (VPR). */
static bool VPR(struct fsm *fsm, struct csi *csi);
/* CSI Ps ; Ps f Horizontal and Vertical Position [row;column] (default = [1,1]) (HVP). */
static bool HVP(struct fsm *fsm, struct csi *csi);
/* CSI Ps g  Tab Clear (TBC). */
static bool TBC(struct fsm *fsm, struct csi *csi);
/* CSI Pm h  Set Mode (SM). */
static bool SM(struct fsm *fsm, struct csi *csi);
/* CSI ? Pm h DEC Private Mode Set (DECSET). */
static bool DECSET(struct fsm *fsm, struct csi *csi);
/* CSI Ps i  Media Copy (MC). */
static bool MC(struct fsm *fsm, struct csi *csi);
/* CSI ? Ps i Media Copy (MC), DEC-specific. */
static bool DECMC(struct fsm *fsm, struct csi *csi);
/* CSI Pm l  Reset Mode (RM). */
static bool RM(struct fsm *fsm, struct csi *csi);
/* CSI ? Pm l DEC Private Mode Reset (DECRST). */
static bool DECRST(struct fsm *fsm, struct csi *csi);
/* CSI Pm m  Character Attributes (SGR). */
static bool SGR(struct fsm *fsm, struct csi *csi);
/* CSI Ps n  Device Status Report (DSR). */
static bool DSR(struct fsm *fsm, struct csi *csi);
/* CSI ? Ps n Device Status Report (DSR, DEC-specific). */
static bool DECDSR(struct fsm *fsm, struct csi *csi);
/* CSI ! p   Soft terminal reset (DECSTR), VT220 and up. */
static bool DECSTR(struct fsm *fsm, struct csi *csi);
/* CSI Pl ; Pc " p Set conformance level (DECSCL), VT220 and up. */
static bool DECSCL(struct fsm *fsm, struct csi *csi);
/* CSI Ps $ p Request ANSI mode (DECRQM). */
/* CSI ? Ps $ p Request DEC private mode (DECRQM).   */
static bool DECRQM(struct fsm *fsm, struct csi *csi);
/* CSI ? Ps $ p Request DEC private mode (DECRQM).   */
static bool DECRQM(struct fsm *fsm, struct csi *csi);
/* CSI Ps q  Load LEDs (DECLL), VT100. */
static bool DECLL(struct fsm *fsm, struct csi *csi);
/* CSI Ps SP q Set cursor style (DECSCUSR), VT520. */
static bool DECSCUSR(struct fsm *fsm, struct csi *csi);
/* CSI Ps " q Select character protection attribute (DECSCA), VT220. */
static bool DECSCA(struct fsm *fsm, struct csi *csi);
/* CSI Ps ; Ps r Set Scrolling Region [top;bottom] (default = full size of window) (DECSTBM), VT100. */
static bool DECSTBM(struct fsm *fsm, struct csi *csi);
/* CSI Pt ; Pl ; Pb ; Pr ; Pm $ r Change Attributes in Rectangular Area (DECCARA), VT400 and up. */
static bool DECCARA(struct fsm *fsm, struct csi *csi);
/* CSI Pl ; Pr s Set left and right margins (DECSLRM), VT420 and up.  This is available only when DECLRMM is enabled. */
static bool DECSLRM(struct fsm *fsm, struct csi *csi);
/* CSI Ps ; Ps ; Ps t Window manipulation (XTWINOPS) */
static bool XTWINOPS(struct fsm *fsm, struct csi *csi);
/* CSI Ps SP t Set warning-bell volume (DECSWBV), VT520. */
static bool DECSWBV(struct fsm *fsm, struct csi *csi);
/* CSI Pt ; Pl ; Pb ; Pr ; Pm $ t Reverse Attributes in Rectangular Area (DECRARA), VT400 and up. */
static bool DECRARA(struct fsm *fsm, struct csi *csi);
/* CSI u     Restore cursor (SCORC, also ANSI.SYS). */
static bool SCORC(struct fsm *fsm, struct csi *csi);
/* CSI & u   User-Preferred Supplemental Set (DECRQUPSS), VT320, VT510. Response is DECAUPSS. */
static bool DECRQUPSS(struct fsm *fsm, struct csi *csi);
/* CSI ? u https://sw.kovidgoyal.net/kitty/keyboard-protocol/ */
static bool KITTY_KEYBOARD_QUERY(struct fsm *fsm, struct csi *csi);
/* CSI Ps SP u Set margin-bell volume (DECSMBV), VT520. */
static bool DECSMBV(struct fsm *fsm, struct csi *csi);
/* CSI " v   Request Displayed Extent (DECRQDE), VT340, VT420. */
static bool DECRQDE(struct fsm *fsm, struct csi *csi);
/* CSI Pt ; Pl ; Pb ; Pr ; Pp ; Pt ; Pl ; Pp $ v Copy Rectangular Area (DECCRA), VT400 and up. */
static bool DECCRA(struct fsm *fsm, struct csi *csi);
/* CSI Ps $ w Request presentation state report (DECRQPSR), VT320 and up. */
static bool DECRQPSR(struct fsm *fsm, struct csi *csi);
/* CSI Pt ; Pl ; Pb ; Pr ' w Enable Filter Rectangle (DECEFR), VT420 and up. */
static bool DECEFR(struct fsm *fsm, struct csi *csi);
/*   CSI Ps x  Request Terminal Parameters (DECREQTPARM). */
static bool DECREQTPARM(struct fsm *fsm, struct csi *csi);
/* CSI Ps * x Select Attribute Change Extent (DECSACE), VT420 and up. */
static bool DECSACE(struct fsm *fsm, struct csi *csi);
/* CSI Pc ; Pt ; Pl ; Pb ; Pr $ x Fill Rectangular Area (DECFRA), VT420 and up. */
static bool DECFRA(struct fsm *fsm, struct csi *csi);
/* CSI Pi ; Pg ; Pt ; Pl ; Pb ; Pr * y Request Checksum of Rectangular Area (DECRQCRA), VT420 and up. */
static bool DECRQCRA(struct fsm *fsm, struct csi *csi);
/* CSI Ps ; Pu ' z Enable Locator Reporting (DECELR). */
static bool DECELR(struct fsm *fsm, struct csi *csi);
/* CSI Pt ; Pl ; Pb ; Pr $ z Erase Rectangular Area (DECERA), VT400 and up. */
static bool DECERA(struct fsm *fsm, struct csi *csi);
/* CSI Pm ' { Select Locator Events (DECSLE). */
static bool DECSLE(struct fsm *fsm, struct csi *csi);
/* CSI Pt ; Pl ; Pb ; Pr $ { Selective Erase Rectangular Area (DECSERA), VT400 and up. */
static bool DECSERA(struct fsm *fsm, struct csi *csi);
/* CSI Ps $ | Select columns per page (DECSCPP), VT340. */
static bool DECSCPP(struct fsm *fsm, struct csi *csi);
/* CSI Ps ' | Request Locator Position (DECRQLP). */
static bool DECRQLP(struct fsm *fsm, struct csi *csi);
/* CSI Ps * | Select number of lines per screen (DECSNLS), VT420 and up. */
static bool DECSNLS(struct fsm *fsm, struct csi *csi);
/* CSI Ps ; Pf ; Pb , | Assign Color (DECAC), VT525 only. */
static bool DECAC(struct fsm *fsm, struct csi *csi);
/* CSI Ps ; Pf ; Pb , } Alternate Text Color (DECATC), VT525 only.  This feature specifies the colors to use when DECSTGLT is selected to 1 or 2. */
static bool DECATC(struct fsm *fsm, struct csi *csi);
/* CSI Ps ' } Insert Ps Column(s) (default = 1) (DECIC), VT420 and up. */
static bool DECIC(struct fsm *fsm, struct csi *csi);
/* CSI Ps $ } Select active status display (DECSASD), VT320 and up. */
static bool DECSASD(struct fsm *fsm, struct csi *csi);
/* CSI Ps ' ~ Delete Ps Column(s) (default = 1) (DECDC), VT420 and up. */
static bool DECDC(struct fsm *fsm, struct csi *csi);
/* CSI Ps $ ~ Select status line type (DECSSDT), VT320 and up. */
static bool DECSSDT(struct fsm *fsm, struct csi *csi);

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

static char *csi_apply_sgr_from_params(struct grid_cell_style *style, int n, struct csi_param *params) {
  // Special case when the 0 is omitted
  if (n == 0) {
    style->attr = 0;
    style->bg = style->fg = color_default;
    return NULL;
  }

  for (int i = 0; i < n; i++) {
    struct csi_param *attribute = &params[i];

    if (attribute->primary == 0) {
      style->attr = 0;
      style->bg = style->fg = color_default;
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
      style->attr |= enable[attribute->primary];
    } else if (attribute->primary == 21) {
      style->attr |= ATTR_UNDERLINE_DOUBLE;
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
      style->attr &= ~disable[attribute->primary % 10];
    } else if (attribute->primary >= 30 && attribute->primary <= 49) {
      struct color *target = attribute->primary >= 40 ? &style->bg : &style->fg;
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
      style->fg = (struct color){.table = bright, .cmd = COLOR_TABLE};
    } else if (attribute->primary >= 100 && attribute->primary <= 107) {
      int bright = 8 + attribute->primary - 100;
      style->bg = (struct color){.table = bright, .cmd = COLOR_TABLE};
    }
  }
  return NULL;
}

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
}

static bool ICH(struct fsm *fsm, struct csi *csi) {
  int count = csi->params[0].primary ? csi->params[0].primary : 1;
  grid_insert_blanks_at_cursor(fsm->active_grid, count);
  return true;
}

bool SL(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("SL"); return false; }

static bool CUU(struct fsm *fsm, struct csi *csi) {
  int count = csi->params[0].primary ? csi->params[0].primary : 1;
  grid_move_cursor(fsm->active_grid, 0, -count);
  return true;
}

bool SR(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("SR"); return false; }

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

static bool CNL(struct fsm *fsm, struct csi *csi) {
  int count = csi->params[0].primary ? csi->params[0].primary : 1;
  grid_move_cursor(fsm->active_grid, 0, count);
  grid_position_cursor_column(fsm->active_grid, 0);
  return true;
}

static bool CPL(struct fsm *fsm, struct csi *csi) {
  int count = csi->params[0].primary ? csi->params[0].primary : 1;
  grid_move_cursor(fsm->active_grid, 0, -count);
  grid_position_cursor_column(fsm->active_grid, 0);
  return true;
}

bool CHA(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("CHA"); return false; }

static bool CUP(struct fsm *fsm, struct csi *csi) {
  int col = csi->params[1].primary ? csi->params[1].primary : 1;
  int row = csi->params[0].primary ? csi->params[0].primary : 1;
  grid_position_visual_cursor(fsm->active_grid, col - 1, row - 1);
  return true;
}

bool CHT(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("CHT"); return false; }

static bool ED(struct fsm *fsm, struct csi *csi) {
  struct grid *g = fsm->active_grid;
  int mode = csi->params[0].primary;
  struct cursor start = g->cursor;
  struct cursor end = g->cursor;

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

bool DECSED(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("DECSED"); return false; }

static bool EL(struct fsm *fsm, struct csi *csi) {
  int mode = csi->params[0].primary;
  struct grid *g = fsm->active_grid;
  struct cursor start = g->cursor;
  struct cursor end = g->cursor;
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

bool DECSEL(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("DECSEL"); return false; }

static bool IL(struct fsm *fsm, struct csi *csi) {
  int count = csi->params[0].primary ? csi->params[0].primary : 1;
  grid_shift_lines(fsm->active_grid, -count);
  return true;
}

static bool DL(struct fsm *fsm, struct csi *csi) {
  int count = csi->params[0].primary ? csi->params[0].primary : 1;
  grid_shift_lines(fsm->active_grid, count);
  return true;
}

static bool DCH(struct fsm *fsm, struct csi *csi) {
  int count = csi->params[0].primary ? csi->params[0].primary : 1;
  grid_shift_from_cursor(fsm->active_grid, count);
  return true;
}

bool SU(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("SU"); return false; }

bool SD(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("SD"); return false; }

bool DECST8C(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("DECST8C"); return false; }

static bool ECH(struct fsm *fsm, struct csi *csi) {
  int clear = csi->params[0].primary ? csi->params[0].primary : 1;
  struct cursor start = fsm->active_grid->cursor;
  struct cursor end = {.row = start.row, .col = start.col + clear};
  grid_erase_between_cursors(fsm->active_grid, start, end);
  return true;
}

bool CBT(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("CBT"); return false; }

bool HPA(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("HPA"); return false; }

bool HPR(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("HPR"); return false; }

static bool REP(struct fsm *fsm, struct csi *csi) {
  struct grid *g = fsm->active_grid;
  int count = csi->params[0].primary ? csi->params[0].primary : 1;
  struct grid_cell repeat = { .symbol = fsm->previous_symbol, .style = g->cursor.brush };
  if (utf8_equals(&repeat.symbol, &utf8_zero)) repeat.symbol = utf8_blank;
  for (int i = 0; i < count; i++) {
    grid_insert(g, repeat, fsm->options.auto_wrap_mode);
  }
  return true;
}

static bool DA_PRIMARY(struct fsm *fsm, struct csi *csi) {
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

bool DA_TERTIARY(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("DA_TERTIARY"); return false; }

bool DA_SECONDARY(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("DA_SECONDARY"); return false; }

static bool VPA(struct fsm *fsm, struct csi *csi) {
  // TODO: Same as HPV, this probably needs to respect 'origin'
  int row = csi->params[0].primary ? csi->params[0].primary : 1;
  grid_position_cursor_row(fsm->active_grid, row - 1);
  return true;
}

bool VPR(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("VPR"); return false; }

static bool HVP(struct fsm *fsm, struct csi *csi) { 
  // TODO: This is not entirely correct. AFACT this should respect 'origin',
  // which is also not yet implemented.
  return CUP(fsm, csi); 
}

bool TBC(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("TBC"); return false; }

static bool SM(struct fsm *fsm, struct csi *csi) {
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

static bool DECSET(struct fsm *fsm, struct csi *csi) {
  struct mouse_options *m = &fsm->options.mouse;
  bool on = csi->final == 'h';
  bool off = csi->final == 'l';
  assert(on || off);

  switch (csi->params[0].primary) {
  case 1: fsm->options.application_mode = on; break;
  case 6: fsm->options.origin_mode = on; break;
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
  case 1005: TODO("UTF8 Mouse Mode"); /* m->utf8_mouse_mode = on; */ break;
  case 1006: TODO("SGR Mouse Mode"); /* m->sgr_mouse_mode = on; */ break;
  case 1007: m->alternate_scroll_mode = on; break;
  case 1016: OMITTED("SGR Pixel Mouse Tracking"); return false;
  case 1001: OMITTED("Hilite mouse tracking"); return false;
  case 1015: OMITTED("urxvt mouse mode"); return false;
  case 9: OMITTED("X10 mouse reporting"); return false;
  default: return csi_dispatch_todo(fsm, csi);
  }
  return true;
}

bool MC(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("MC"); return false; }

bool DECMC(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("DECMC"); return false; }

static bool RM(struct fsm *fsm, struct csi *csi) { return SM(fsm, csi); }

static bool DECRST(struct fsm *fsm, struct csi *csi) { return DECSET(fsm, csi); }

static bool SGR(struct fsm *fsm, struct csi *csi) {
  char *error = csi_apply_sgr_from_params(&fsm->active_grid->cursor.brush, csi->n_params, csi->params);
  if (error) {
    logmsg("Error parsing SGR: %.*s: %s", fsm->command_buffer.len - 1, fsm->command_buffer.content + 1, error);
    return false;
  }

  return true;
}

bool DSR(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("DSR"); return false; }

bool DECDSR(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("DECDSR"); return false; }

bool DECSTR(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("DECSTR"); return false; }

bool DECSCL(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("DECSCL"); return false; }

bool DECRQM(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("DECRQM"); return false; }

bool DECLL(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("DECLL"); return false; }

static bool DECSCUSR(struct fsm *fsm, struct csi *csi) {
  int cursor = csi->params[0].primary;
  if (cursor >= CURSOR_STYLE_BLINKING_BLOCK && cursor < CURSOR_STYLE_LAST) {
    fsm->options.cursor.style = cursor;
  } else {
    OMITTED("Unknown cursor style %d", cursor);
  }
  return true;
}

bool DECSCA(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("DECSCA"); return false; }

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

bool DECCARA(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("DECCARA"); return false; }

bool DECSLRM(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("DECSLRM"); return false; }

static bool XTWINOPS(struct fsm *fsm, struct csi *csi) {
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

bool DECSWBV(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("DECSWBV"); return false; }

bool DECRARA(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("DECRARA"); return false; }

bool SCORC(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("SCORC"); return false; }

bool DECRQUPSS(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("DECRQUPSS"); return false; }

bool KITTY_KEYBOARD_QUERY(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("KITTY_KEYBOARD_QUERY"); return false; }

bool DECSMBV(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("DECSMBV"); return false; }

bool DECRQDE(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("DECRQDE"); return false; }

bool DECCRA(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("DECCRA"); return false; }

bool DECRQPSR(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("DECRQPSR"); return false; }

bool DECEFR(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("DECEFR"); return false; }

bool DECREQTPARM(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("DECREQTPARM"); return false; }

bool DECSACE(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("DECSACE"); return false; }

bool DECFRA(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("DECFRA"); return false; }

bool DECRQCRA(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("DECRQCRA"); return false; }

bool DECELR(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("DECELR"); return false; }

bool DECERA(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("DECERA"); return false; }

bool DECSLE(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("DECSLE"); return false; }

bool DECSERA(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("DECSERA"); return false; }

bool DECSCPP(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("DECSCPP"); return false; }

bool DECRQLP(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("DECRQLP"); return false; }

bool DECSNLS(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("DECSNLS"); return false; }

bool DECAC(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("DECAC"); return false; }

bool DECATC(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("DECATC"); return false; }

bool DECIC(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("DECIC"); return false; }

bool DECSASD(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("DECSASD"); return false; }

bool DECDC(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("DECDC"); return false; }

bool DECSSDT(struct fsm *fsm, struct csi *csi) { (void)fsm, (void)csi; TODO("DECSSDT"); return false; }

