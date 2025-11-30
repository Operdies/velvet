#include "csi.h"
#include "vte.h"
#include "utils.h"

enum DECRQM_QUERY_RESPONSE {
  DECRQM_NOT_RECOGNIZED = 0,
  DECRQM_SET = 1,
  DECRQM_RESET = 2,
  DECRQM_PERMANENTLY_SET = 3,
  DECRQM_PERMANENTLY_RESET = 4,
};


/* CSI Ps @  Insert Ps (Blank) Character(s) (default = 1) (ICH). */
static bool ICH(struct vte *vte, struct csi *csi);
/* CSI Ps SP @ Shift left Ps columns(s) (default = 1) (SL), ECMA-48. */
static bool SL(struct vte *vte, struct csi *csi);
/* CSI Ps A  Cursor Up Ps Times (default = 1) (CUU). */
static bool CUU(struct vte *vte, struct csi *csi);
/* CSI Ps SP A Shift right Ps columns(s) (default = 1) (SR), ECMA-48. */
static bool SR(struct vte *vte, struct csi *csi);
/* CSI Ps B  Cursor Down Ps Times (default = 1) (CUD). */
static bool CUD(struct vte *vte, struct csi *csi);
/* CSI Ps C  Cursor Forward Ps Times (default = 1) (CUF). */
static bool CUF(struct vte *vte, struct csi *csi);
/* CSI Ps D  Cursor Backward Ps Times (default = 1) (CUB). */
static bool CUB(struct vte *vte, struct csi *csi);
/* CSI Ps E  Cursor Next Line Ps Times (default = 1) (CNL). */
static bool CNL(struct vte *vte, struct csi *csi);
/* CSI Ps F  Cursor Preceding Line Ps Times (default = 1) (CPL). */
static bool CPL(struct vte *vte, struct csi *csi);
/* CSI Ps G  Cursor Character Absolute  [column] (default = [row,1]) (CHA). */
static bool CHA(struct vte *vte, struct csi *csi);
/* CSI Ps ; Ps H Cursor Position [row;column] (default = [1,1]) (CUP). */
static bool CUP(struct vte *vte, struct csi *csi);
/* CSI Ps I  Cursor Forward Tabulation Ps tab stops (default = 1) (CHT). */
static bool CHT(struct vte *vte, struct csi *csi);
/* CSI Ps J  Erase in Display (ED), VT100. */
static bool ED(struct vte *vte, struct csi *csi);
/* CSI ? Ps J Erase in Display (DECSED), VT220. */
static bool DECSED(struct vte *vte, struct csi *csi);
/* CSI Ps K  Erase in Line (EL), VT100. */
static bool EL(struct vte *vte, struct csi *csi);
/* CSI ? Ps K Erase in Line (DECSEL), VT220. */
static bool DECSEL(struct vte *vte, struct csi *csi);
/* CSI Ps L  Insert Ps Line(s) (default = 1) (IL). */
static bool IL(struct vte *vte, struct csi *csi);
/* CSI Ps M  Delete Ps Line(s) (default = 1) (DL). */
static bool DL(struct vte *vte, struct csi *csi);
/* CSI Ps P  Delete Ps Character(s) (default = 1) (DCH). */
static bool DCH(struct vte *vte, struct csi *csi);
/* CSI Ps S  Scroll up Ps lines (default = 1) (SU), VT420, ECMA-48. */
static bool SU(struct vte *vte, struct csi *csi);
/* CSI Ps T  Scroll down Ps lines (default = 1) (SD), VT420. */
/* CSI Ps ^  Scroll down Ps lines (default = 1) (SD), ECMA-48.
          This was a publication error in the original ECMA-48 5th
          edition (1991) corrected in 2003. */
static bool SD(struct vte *vte, struct csi *csi);
/* CSI ? 5 W Reset tab stops to start with column 9, every 8 columns (DECST8C), VT510. */
static bool DECST8C(struct vte *vte, struct csi *csi);
/* CSI Ps X  Erase Ps Character(s) (default = 1) (ECH). */
static bool ECH(struct vte *vte, struct csi *csi);
/* CSI Ps Z  Cursor Backward Tabulation Ps tab stops (default = 1) (CBT). */
static bool CBT(struct vte *vte, struct csi *csi);
/* CSI Ps ^  Scroll down Ps lines (default = 1) (SD), ECMA-48. */
static bool SD(struct vte *vte, struct csi *csi);
/* CSI Ps `  Character Position Absolute  [column] (default = [row,1]) (HPA). */
static bool HPA(struct vte *vte, struct csi *csi);
/* CSI Ps a  Character Position Relative  [columns] (default = [row,col+1]) (HPR). */
static bool HPR(struct vte *vte, struct csi *csi);
/* CSI Ps b  Repeat the preceding graphic character Ps times (REP). */
static bool REP(struct vte *vte, struct csi *csi);
/* CSI Ps c  Send Device Attributes (Primary DA). */
static bool DA_PRIMARY(struct vte *vte, struct csi *csi);
/* CSI = Ps c Send Device Attributes (Tertiary DA). */
static bool DA_TERTIARY(struct vte *vte, struct csi *csi);
/* CSI > Ps c Send Device Attributes (Secondary DA). */
static bool DA_SECONDARY(struct vte *vte, struct csi *csi);
/* CSI Ps d  Line Position Absolute  [row] (default = [1,column]) (VPA). */
static bool VPA(struct vte *vte, struct csi *csi);
/* CSI Ps e  Line Position Relative  [rows] (default = [row+1,column]) (VPR). */
static bool VPR(struct vte *vte, struct csi *csi);
/* CSI Ps ; Ps f Horizontal and Vertical Position [row;column] (default = [1,1]) (HVP). */
static bool HVP(struct vte *vte, struct csi *csi);
/* CSI Ps g  Tab Clear (TBC). */
static bool TBC(struct vte *vte, struct csi *csi);
/* CSI Pm h  Set Mode (SM). */
static bool SM(struct vte *vte, struct csi *csi);
/* CSI ? Pm h DEC Private Mode Set (DECSET). */
static bool DECSET(struct vte *vte, struct csi *csi);
/* CSI Ps i  Media Copy (MC). */
static bool MC(struct vte *vte, struct csi *csi);
/* CSI ? Ps i Media Copy (MC), DEC-specific. */
static bool DECMC(struct vte *vte, struct csi *csi);
/* CSI Pm l  Reset Mode (RM). */
static bool RM(struct vte *vte, struct csi *csi);
/* CSI ? Pm l DEC Private Mode Reset (DECRST). */
static bool DECRST(struct vte *vte, struct csi *csi);
/* CSI Pm m  Character Attributes (SGR). */
static bool SGR(struct vte *vte, struct csi *csi);
/* CSI Ps n  Device Status Report (DSR). */
static bool DSR(struct vte *vte, struct csi *csi);
/* CSI ? Ps n Device Status Report (DSR, DEC-specific). */
static bool DECDSR(struct vte *vte, struct csi *csi);
/* CSI ! p   Soft terminal reset (DECSTR), VT220 and up. */
static bool DECSTR(struct vte *vte, struct csi *csi);
/* CSI Pl ; Pc " p Set conformance level (DECSCL), VT220 and up. */
static bool DECSCL(struct vte *vte, struct csi *csi);
/* CSI Ps $ p Request ANSI mode (DECRQM). */
/* CSI ? Ps $ p Request DEC private mode (DECRQM).   */
static bool DECRQM(struct vte *vte, struct csi *csi);
/* CSI Ps q  Load LEDs (DECLL), VT100. */
static bool DECLL(struct vte *vte, struct csi *csi);
/* CSI Ps SP q Set cursor style (DECSCUSR), VT520. */
static bool DECSCUSR(struct vte *vte, struct csi *csi);
/* CSI Ps " q Select character protection attribute (DECSCA), VT220. */
static bool DECSCA(struct vte *vte, struct csi *csi);
/* CSI Ps ; Ps r Set Scrolling Region [top;bottom] (default = full size of window) (DECSTBM), VT100. */
static bool DECSTBM(struct vte *vte, struct csi *csi);
/* CSI Pt ; Pl ; Pb ; Pr ; Pm $ r Change Attributes in Rectangular Area (DECCARA), VT400 and up. */
static bool DECCARA(struct vte *vte, struct csi *csi);
/* CSI Pl ; Pr s Set left and right margins (DECSLRM), VT420 and up.  This is available only when DECLRMM is enabled. */
static bool DECSLRM(struct vte *vte, struct csi *csi);
/* CSI Ps ; Ps ; Ps t Window manipulation (XTWINOPS) */
static bool XTWINOPS(struct vte *vte, struct csi *csi);
/* CSI Ps SP t Set warning-bell volume (DECSWBV), VT520. */
static bool DECSWBV(struct vte *vte, struct csi *csi);
/* CSI Pt ; Pl ; Pb ; Pr ; Pm $ t Reverse Attributes in Rectangular Area (DECRARA), VT400 and up. */
static bool DECRARA(struct vte *vte, struct csi *csi);
/* CSI u     Restore cursor (SCORC, also ANSI.SYS). */
static bool SCORC(struct vte *vte, struct csi *csi);
/* CSI & u   User-Preferred Supplemental Set (DECRQUPSS), VT320, VT510. Response is DECAUPSS. */
static bool DECRQUPSS(struct vte *vte, struct csi *csi);
/* CSI ? u https://sw.kovidgoyal.net/kitty/keyboard-protocol/ */
static bool KITTY_KEYBOARD_QUERY(struct vte *vte, struct csi *csi);
/* CSI Ps + T https://sw.kovidgoyal.net/kitty/unscroll/ */
static bool KITTY_UNSCROLL(struct vte *vte, struct csi *csi);
/* CSI Ps SP u Set margin-bell volume (DECSMBV), VT520. */
static bool DECSMBV(struct vte *vte, struct csi *csi);
/* CSI " v   Request Displayed Extent (DECRQDE), VT340, VT420. */
static bool DECRQDE(struct vte *vte, struct csi *csi);
/* CSI Pt ; Pl ; Pb ; Pr ; Pp ; Pt ; Pl ; Pp $ v Copy Rectangular Area (DECCRA), VT400 and up. */
static bool DECCRA(struct vte *vte, struct csi *csi);
/* CSI Ps $ w Request presentation state report (DECRQPSR), VT320 and up. */
static bool DECRQPSR(struct vte *vte, struct csi *csi);
/* CSI Pt ; Pl ; Pb ; Pr ' w Enable Filter Rectangle (DECEFR), VT420 and up. */
static bool DECEFR(struct vte *vte, struct csi *csi);
/*   CSI Ps x  Request Terminal Parameters (DECREQTPARM). */
static bool DECREQTPARM(struct vte *vte, struct csi *csi);
/* CSI Ps * x Select Attribute Change Extent (DECSACE), VT420 and up. */
static bool DECSACE(struct vte *vte, struct csi *csi);
/* CSI Pc ; Pt ; Pl ; Pb ; Pr $ x Fill Rectangular Area (DECFRA), VT420 and up. */
static bool DECFRA(struct vte *vte, struct csi *csi);
/* CSI Pi ; Pg ; Pt ; Pl ; Pb ; Pr * y Request Checksum of Rectangular Area (DECRQCRA), VT420 and up. */
static bool DECRQCRA(struct vte *vte, struct csi *csi);
/* CSI Ps ; Pu ' z Enable Locator Reporting (DECELR). */
static bool DECELR(struct vte *vte, struct csi *csi);
/* CSI Pt ; Pl ; Pb ; Pr $ z Erase Rectangular Area (DECERA), VT400 and up. */
static bool DECERA(struct vte *vte, struct csi *csi);
/* CSI Pm ' { Select Locator Events (DECSLE). */
static bool DECSLE(struct vte *vte, struct csi *csi);
/* CSI Pt ; Pl ; Pb ; Pr $ { Selective Erase Rectangular Area (DECSERA), VT400 and up. */
static bool DECSERA(struct vte *vte, struct csi *csi);
/* CSI Ps $ | Select columns per page (DECSCPP), VT340. */
static bool DECSCPP(struct vte *vte, struct csi *csi);
/* CSI Ps ' | Request Locator Position (DECRQLP). */
static bool DECRQLP(struct vte *vte, struct csi *csi);
/* CSI Ps * | Select number of lines per screen (DECSNLS), VT420 and up. */
static bool DECSNLS(struct vte *vte, struct csi *csi);
/* CSI Ps ; Pf ; Pb , | Assign Color (DECAC), VT525 only. */
static bool DECAC(struct vte *vte, struct csi *csi);
/* CSI Ps ; Pf ; Pb , } Alternate Text Color (DECATC), VT525 only.  This feature specifies the colors to use when DECSTGLT is selected to 1 or 2. */
static bool DECATC(struct vte *vte, struct csi *csi);
/* CSI Ps ' } Insert Ps Column(s) (default = 1) (DECIC), VT420 and up. */
static bool DECIC(struct vte *vte, struct csi *csi);
/* CSI Ps $ } Select active status display (DECSASD), VT320 and up. */
static bool DECSASD(struct vte *vte, struct csi *csi);
/* CSI Ps ' ~ Delete Ps Column(s) (default = 1) (DECDC), VT420 and up. */
static bool DECDC(struct vte *vte, struct csi *csi);
/* CSI Ps $ ~ Select status line type (DECSSDT), VT320 and up. */
static bool DECSSDT(struct vte *vte, struct csi *csi);

static char *byte_names[UINT8_MAX + 1] = {
    [' '] = "SP",
    ['\a'] = "BEL",
    ['\r'] = "CR",
    ['\b'] = "BS",
    ['\f'] = "FF",
    ['\n'] = "LF",
};

static bool csi_dispatch_todo(struct vte *vte, struct csi *csi) {
  (void)vte;
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

static bool csi_dispatch_omitted(struct vte *vte, struct csi *csi) {
  (void)vte, (void)csi;
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

static char *csi_apply_sgr_from_params(struct screen_cell_style *style, int n, struct csi_param *params) {
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
          // if n_sub > 4, that means colorspace was specified. We always ignore colorspace, but because it preceedes
          // the rgb values, the indices are shifted if it is present.
          int start = attribute->n_sub > 4 ? 2 : 1;
          int red = attribute->sub[start];
          int green = attribute->sub[start + 1];
          int blue = attribute->sub[start + 2];
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

bool csi_dispatch(struct vte *vte, struct csi *csi) {
  assert(csi->state == CSI_ACCEPT);

  // convert the three byte values into a single u32 value
#define KEY(leading, intermediate, final)                                                                              \
  ((((uint32_t)leading) << 16) | (((uint32_t)intermediate) << 8) | (((uint32_t) final)))

  switch (KEY(csi->leading, csi->intermediate, csi->final)) {
    // convert each CSI item from csi.def into a switch case
#define CSI(l, i, f, fn, _)                                                                                            \
  case KEY(l, i, f): return fn(vte, csi);
#include "csi.def"
#undef CSI
  default: return csi_dispatch_todo(vte, csi);
  }
}

static bool ICH(struct vte *vte, struct csi *csi) {
  int count = csi->params[0].primary ? csi->params[0].primary : 1;
  screen_insert_blanks_at_cursor(vte_get_current_screen(vte), count);
  return true;
}

bool SL(struct vte *vte, struct csi *csi) { (void)vte, (void)csi; TODO("SL"); return false; }

static bool CUU(struct vte *vte, struct csi *csi) {
  int count = csi->params[0].primary ? csi->params[0].primary : 1;
  screen_move_cursor_relative(vte_get_current_screen(vte), 0, -count);
  return true;
}

bool SR(struct vte *vte, struct csi *csi) { (void)vte, (void)csi; TODO("SR"); return false; }

static bool CUD(struct vte *vte, struct csi *csi) {
  int count = csi->params[0].primary ? csi->params[0].primary : 1;
  screen_move_cursor_relative(vte_get_current_screen(vte), 0, count);
  return true;
}

static bool CUF(struct vte *vte, struct csi *csi) {
  int count = csi->params[0].primary ? csi->params[0].primary : 1;
  screen_move_cursor_relative(vte_get_current_screen(vte), count, 0);
  return true;
}

static bool CUB(struct vte *vte, struct csi *csi) {
  int count = csi->params[0].primary ? csi->params[0].primary : 1;
  screen_move_cursor_relative(vte_get_current_screen(vte), -count, 0);
  return true;
}

static bool CNL(struct vte *vte, struct csi *csi) {
  int count = csi->params[0].primary ? csi->params[0].primary : 1;
  screen_move_cursor_relative(vte_get_current_screen(vte), 0, count);
  screen_set_cursor_column(vte_get_current_screen(vte), 0);
  return true;
}

static bool CPL(struct vte *vte, struct csi *csi) {
  int count = csi->params[0].primary ? csi->params[0].primary : 1;
  screen_move_cursor_relative(vte_get_current_screen(vte), 0, -count);
  screen_set_cursor_column(vte_get_current_screen(vte), 0);
  return true;
}

bool CHA(struct vte *vte, struct csi *csi) { 
  int count = csi->params[0].primary ? csi->params[0].primary : 1;
  screen_set_cursor_column(vte_get_current_screen(vte), count - 1);
  return true;
}

static bool CUP(struct vte *vte, struct csi *csi) {
  int col = csi->params[1].primary ? csi->params[1].primary : 1;
  int row = csi->params[0].primary ? csi->params[0].primary : 1;
  screen_set_cursor_position(vte_get_current_screen(vte), col - 1, row - 1);
  return true;
}

bool CHT(struct vte *vte, struct csi *csi) { (void)vte, (void)csi; TODO("CHT"); return false; }

static bool ED(struct vte *vte, struct csi *csi) {
  struct screen *g = vte_get_current_screen(vte);
  int mode = csi->params[0].primary;
  struct cursor start = g->cursor;
  struct cursor end = g->cursor;

  switch (mode) {
  case 1: // Erase from start of screen to cursor
    start.column = screen_left(g);
    start.row = screen_top(g);
    break;
  case 2: // Erase entire screen
    start.column = screen_left(g);
    start.row = screen_top(g);
    end.column = screen_right(g);
    end.row = screen_bottom(g);
    break;
  case 3: // erase scrollback
    return csi_dispatch_todo(vte, csi);
  case 0:
  default: // erase from cursor to end of screen
    end.column = screen_right(g);
    end.row = screen_bottom(g);
    break;
  }

  screen_erase_between_cursors(g, start, end);

  return true;
}

bool DECSED(struct vte *vte, struct csi *csi) { (void)vte, (void)csi; TODO("DECSED"); return false; }

static bool EL(struct vte *vte, struct csi *csi) {
  int mode = csi->params[0].primary;
  struct screen *g = vte_get_current_screen(vte);
  struct cursor start = g->cursor;
  struct cursor end = g->cursor;
  switch (mode) {
  case 0: end.column = screen_right(g); break;     // erase from cursor to end
  case 1: start.column = screen_left(g); break; // erase from start to cursor
  case 2:                                   // erase entire line
    start.column = screen_left(g);
    end.column = screen_right(g);
    break;
  default: return csi_dispatch_todo(vte, csi);
  }
  screen_erase_between_cursors(g, start, end);
  return true;
}

bool DECSEL(struct vte *vte, struct csi *csi) { (void)vte, (void)csi; TODO("DECSEL"); return false; }

static bool IL(struct vte *vte, struct csi *csi) {
  int count = csi->params[0].primary ? csi->params[0].primary : 1;
  screen_insert_lines(vte_get_current_screen(vte), count);
  screen_set_cursor_column(vte_get_current_screen(vte), 0);
  return true;
}

static bool DL(struct vte *vte, struct csi *csi) {
  int count = csi->params[0].primary ? csi->params[0].primary : 1;
  screen_delete_lines(vte_get_current_screen(vte), count);
  screen_set_cursor_column(vte_get_current_screen(vte), 0);
  return true;
}

static bool DCH(struct vte *vte, struct csi *csi) {
  int count = csi->params[0].primary ? csi->params[0].primary : 1;
  screen_shift_from_cursor(vte_get_current_screen(vte), count);
  return true;
}

bool SU(struct vte *vte, struct csi *csi) { 
  int count = csi->params[0].primary ? csi->params[0].primary : 1;
  struct screen *g = vte_get_current_screen(vte);
  screen_shuffle_rows_up(g, count, g->scroll_top, g->scroll_bottom);
  return true;
}

bool SD(struct vte *vte, struct csi *csi) { 
  int count = csi->params[0].primary ? csi->params[0].primary : 1;
  struct screen *g = vte_get_current_screen(vte);
  screen_shuffle_rows_down(g, count, g->scroll_top, g->scroll_bottom);
  return true;
}

bool DECST8C(struct vte *vte, struct csi *csi) { (void)vte, (void)csi; TODO("DECST8C"); return false; }

static bool ECH(struct vte *vte, struct csi *csi) {
  int count = csi->params[0].primary ? csi->params[0].primary : 1;
  struct screen *s = vte_get_current_screen(vte);
  int first_column, last_column;
  first_column = s->cursor.column;
  // subtract 1 from last column because the erase function is inclusive
  last_column = first_column + count - 1;
  struct cursor start = {.row = s->cursor.row, .column = first_column};
  struct cursor end = {.row = s->cursor.row, .column = last_column};
  screen_erase_between_cursors(s, start, end);
  return true;
}

bool CBT(struct vte *vte, struct csi *csi) { (void)vte, (void)csi; TODO("CBT"); return false; }

bool HPA(struct vte *vte, struct csi *csi) { (void)vte, (void)csi; TODO("HPA"); return false; }

bool HPR(struct vte *vte, struct csi *csi) { (void)vte, (void)csi; TODO("HPR"); return false; }

static bool REP(struct vte *vte, struct csi *csi) {
  struct screen *g = vte_get_current_screen(vte);
  int count = csi->params[0].primary ? csi->params[0].primary : 1;
  struct screen_cell repeat = { .symbol = vte->previous_symbol, .style = g->cursor.brush };
  if (utf8_equals(&repeat.symbol, &utf8_zero)) repeat.symbol = utf8_blank;
  for (int i = 0; i < count; i++) {
    screen_insert(g, repeat, vte->options.auto_wrap_mode);
  }
  return true;
}

static bool DA_PRIMARY(struct vte *vte, struct csi *csi) {
  switch (csi->params[0].primary) {
  case 0: {
    vte_send_device_attributes(vte);
    return true;
  } break;
  default: return csi_dispatch_todo(vte, csi);
  }
}

bool DA_SECONDARY(struct vte *vte, struct csi *csi) {
  switch (csi->params[0].primary) {
  case 0: {
      // https://invisible-island.net/xterm/ctlseqs/ctlseqs.html#h3-Functions-using-CSI-_-ordered-by-the-final-character_s_
      // CSI  > Pp ; Pv ; Pc c
      // Pp = terminal type, where '1' means VT220
      // Pv = firmware version. Ghostty responds with 10, so it must be good.
      // Pc indicates the ROM cartridge registration number and is always zero
    string_push(&vte->pending_input, u8"\x1b[>1;10;0c");
    return true;
  } break;
  default: return csi_dispatch_todo(vte, csi);
  }
}

bool DA_TERTIARY(struct vte *vte, struct csi *csi) { (void)vte, (void)csi; OMITTED("DA_TERTIARY (VT400 and up)"); return false; }

static bool VPA(struct vte *vte, struct csi *csi) {
  // TODO: Same as HPV, this probably needs to respect 'origin'
  int row = csi->params[0].primary ? csi->params[0].primary : 1;
  screen_set_cursor_row(vte_get_current_screen(vte), row - 1);
  return true;
}

bool VPR(struct vte *vte, struct csi *csi) {
  int row = csi->params[0].primary ? csi->params[0].primary : 1;
  screen_move_cursor_relative(vte_get_current_screen(vte), 0, row);
  return true;
}

static bool HVP(struct vte *vte, struct csi *csi) { return CUP(vte, csi); }

bool TBC(struct vte *vte, struct csi *csi) { (void)vte, (void)csi; TODO("TBC"); return false; }

static bool SM(struct vte *vte, struct csi *csi) {
  bool on = csi->final == 'h';
  bool off = csi->final == 'l';
  assert(on || off);

  int mode = csi->params[0].primary;
  switch (mode) {
  case 2: TODO("Keyboard Action Mode (KAM)"); return false;
  case 4: TODO("Insert Mode (IRM)"); return false;
  case 12: OMITTED("Send/receive (SRM)"); return false;
  case 20: vte->options.auto_return = on; return true;
  default: TODO("Set Mode %d", mode); return false;
  }
}

static void set_cursor_blinking(struct vte *vte, bool blinking) {
  if (blinking) {
    switch (vte->options.cursor.style) {
    case CURSOR_STYLE_DEFAULT:
    case CURSOR_STYLE_STEADY_BLOCK: vte->options.cursor.style = CURSOR_STYLE_BLINKING_BLOCK; break;
    case CURSOR_STYLE_STEADY_UNDERLINE: vte->options.cursor.style = CURSOR_STYLE_BLINKING_UNDERLINE; break;
    case CURSOR_STYLE_STEADY_BAR: vte->options.cursor.style = CURSOR_STYLE_BLINKING_BAR; break;
    default: break;
    }
  } else {
    switch (vte->options.cursor.style) {
    case CURSOR_STYLE_DEFAULT:
    case CURSOR_STYLE_BLINKING_BLOCK: vte->options.cursor.style = CURSOR_STYLE_STEADY_BLOCK; break;
    case CURSOR_STYLE_BLINKING_UNDERLINE: vte->options.cursor.style = CURSOR_STYLE_STEADY_UNDERLINE; break;
    case CURSOR_STYLE_BLINKING_BAR: vte->options.cursor.style = CURSOR_STYLE_STEADY_BAR; break;
    default: break;
    }
  }
}

#define resp(x) ((x) ? DECRQM_SET : DECRQM_RESET)
static enum DECRQM_QUERY_RESPONSE query_ansi_mode(struct vte *vte, int mode) {
  struct emulator_options o = vte->options;
  switch (mode) {
  case 20: return resp(o.auto_return);
  default: return DECRQM_NOT_RECOGNIZED;
  }
}

static enum DECRQM_QUERY_RESPONSE query_private_mode(struct vte *vte, int mode) {
  struct emulator_options o = vte->options;
  switch (mode) {
  case 1: return resp(o.application_mode);
  case 6: return resp(o.origin_mode);
  case 7: return resp(o.auto_wrap_mode);
  case 12: return resp(o.cursor.style & 1);
  case 25: return resp(o.cursor.visible);
  case 1004: return resp(o.focus_reporting);
  case 1049: return resp(o.alternate_screen);
  case 2004: return resp(o.bracketed_paste);
  case 9:
  case 1000:
  case 1002:
  case 1003: return resp((int)o.mouse.tracking == mode);
  case 1005:
  case 1006:
  case 1015:
  case 1016: return resp((int)o.mouse.mode == mode);
  case 1007: return resp(o.mouse.alternate_scroll_mode);
  default: return DECRQM_NOT_RECOGNIZED;
  }
}
#undef resp

static bool DECSET(struct vte *vte, struct csi *csi) {
  struct mouse_options *m = &vte->options.mouse;
  bool on = csi->final == 'h';
  bool off = csi->final == 'l';
  assert(on || off);

  int mode = csi->params[0].primary;

  switch (mode) {
  case 1: vte->options.application_mode = on; break;
  case 6: vte->options.origin_mode = on; break;
  case 7: vte->options.auto_wrap_mode = on; break;
  case 12: set_cursor_blinking(vte, on); break;
  case 25: vte->options.cursor.visible = on; break;
  case 1004: vte->options.focus_reporting = on; break;
  case 1049: {
    if (on)
      vte_enter_alternate_screen(vte);
    else
      vte_enter_primary_screen(vte);
  } break;
  case 2004: vte->options.bracketed_paste = on; break;
  case 1007: m->alternate_scroll_mode = on; break;
  case 9:
  case 1000:
  case 1001:
  case 1002:
  case 1003: {
    if (on) {
      m->tracking = mode;
    } else {
      // mouse tracking should only reset if the specified mode was active
      if ((int)m->tracking == mode) {
        m->tracking = MOUSE_TRACKING_OFF;
        m->mode = MOUSE_MODE_DEFAULT;
      }
    }
  }
    return false;
  case 1005:
  case 1006:
  case 1015:
  case 1016: {
    if (on) {
      m->mode = mode;
    } else {
      if ((int)m->mode == mode) {
        m->mode = MOUSE_MODE_DEFAULT;
      }
    }
  } break;
  case 2026:
    TODO("Synchronized rendering: "
         "https://github.com/contour-terminal/vt-extensions/blob/master/synchronized-output.md");
    return false;
  default: return csi_dispatch_todo(vte, csi);
  }
  return true;
}

bool MC(struct vte *vte, struct csi *csi) { (void)vte, (void)csi; TODO("MC"); return false; }

bool DECMC(struct vte *vte, struct csi *csi) { (void)vte, (void)csi; TODO("DECMC"); return false; }

static bool RM(struct vte *vte, struct csi *csi) { return SM(vte, csi); }

static bool DECRST(struct vte *vte, struct csi *csi) { return DECSET(vte, csi); }

static bool SGR(struct vte *vte, struct csi *csi) {
  char *error = csi_apply_sgr_from_params(&vte_get_current_screen(vte)->cursor.brush, csi->n_params, csi->params);
  if (error) {
    logmsg("Error parsing SGR: %.*s: %s", vte->command_buffer.len - 1, vte->command_buffer.content + 1, error);
    return false;
  }

  return true;
}

bool DSR(struct vte *vte, struct csi *csi) {
  int n = csi->params[0].primary;
  vte_send_status_report(vte, n);
  return true;
}

bool DECDSR(struct vte *vte, struct csi *csi) { (void)vte, (void)csi; TODO("DECDSR"); return false; }

bool DECSTR(struct vte *vte, struct csi *csi) { (void)vte, (void)csi; TODO("DECSTR"); return false; }

bool DECSCL(struct vte *vte, struct csi *csi) { (void)vte, (void)csi; TODO("DECSCL"); return false; }

bool DECRQM(struct vte *vte, struct csi *csi) { 
  int mode = csi->params[0].primary;
  enum DECRQM_QUERY_RESPONSE r = csi->leading == '?' ? query_private_mode(vte, mode) : query_ansi_mode(vte, mode);
  string_push_csi(&vte->pending_input, csi->leading, INT_SLICE(mode, r), "$y");
  if (r == DECRQM_NOT_RECOGNIZED) {
    TODO("Query unrecognized mode: %d", mode);
  }
  return true;
}

bool DECLL(struct vte *vte, struct csi *csi) { (void)vte, (void)csi; TODO("DECLL"); return false; }

static bool DECSCUSR(struct vte *vte, struct csi *csi) {
  int cursor = csi->params[0].primary;
  if (cursor >= CURSOR_STYLE_DEFAULT && cursor < CURSOR_STYLE_LAST) {
    vte->options.cursor.style = cursor;
  } else {
    OMITTED("Unknown cursor style %d", cursor);
  }
  return true;
}

bool DECSCA(struct vte *vte, struct csi *csi) { (void)vte, (void)csi; TODO("DECSCA"); return false; }

static bool DECSTBM(struct vte *vte, struct csi *csi) {
  int top, bottom;
  top = csi->params[0].primary;
  bottom = csi->params[1].primary;

  if (bottom == 0) bottom = vte->rows;
  if (top > 0) top--;
  if (bottom > 0) bottom--;

  struct screen *g = vte_get_current_screen(vte);
  screen_set_scroll_region(g, top, bottom);
  screen_set_cursor_column(g, 0);
  screen_set_cursor_row(g, vte->options.origin_mode ? top : 0);
  return true;
}

bool DECCARA(struct vte *vte, struct csi *csi) { (void)vte, (void)csi; TODO("DECCARA"); return false; }

bool DECSLRM(struct vte *vte, struct csi *csi) { (void)vte, (void)csi; TODO("DECSLRM"); return false; }

static bool XTWINOPS(struct vte *vte, struct csi *csi) {
  switch (csi->params[0].primary) {
  case 14: TODO("Report Text Area Size in Pixels"); return false;
  case 16: TODO("Report Cell Size in Pixels"); return false;
  case 18: TODO("Report Text Area Size in Characters"); return false;
  default:
    /* The rest of the commands in this group manipulate the window or otherwise interface with the windowing system.
     * Regardless of whether or not the underlying terminal emulator supports it, */
    return csi_dispatch_omitted(vte, csi);
  }
}

bool DECSWBV(struct vte *vte, struct csi *csi) { (void)vte, (void)csi; TODO("DECSWBV"); return false; }

bool DECRARA(struct vte *vte, struct csi *csi) { (void)vte, (void)csi; TODO("DECRARA"); return false; }

bool SCORC(struct vte *vte, struct csi *csi) { (void)vte, (void)csi; TODO("SCORC"); return false; }

bool DECRQUPSS(struct vte *vte, struct csi *csi) { (void)vte, (void)csi; TODO("DECRQUPSS"); return false; }

bool KITTY_KEYBOARD_QUERY(struct vte *vte, struct csi *csi) { (void)vte, (void)csi; TODO("KITTY_KEYBOARD_QUERY"); return false; }

bool KITTY_UNSCROLL(struct vte *vte, struct csi *csi) { (void)vte, (void)csi; TODO("KITTY_UNSCROLL"); return false; }

bool DECSMBV(struct vte *vte, struct csi *csi) { (void)vte, (void)csi; TODO("DECSMBV"); return false; }

bool DECRQDE(struct vte *vte, struct csi *csi) { (void)vte, (void)csi; TODO("DECRQDE"); return false; }

bool DECCRA(struct vte *vte, struct csi *csi) { (void)vte, (void)csi; TODO("DECCRA"); return false; }

bool DECRQPSR(struct vte *vte, struct csi *csi) { (void)vte, (void)csi; TODO("DECRQPSR"); return false; }

bool DECEFR(struct vte *vte, struct csi *csi) { (void)vte, (void)csi; TODO("DECEFR"); return false; }

bool DECREQTPARM(struct vte *vte, struct csi *csi) { (void)vte, (void)csi; TODO("DECREQTPARM"); return false; }

bool DECSACE(struct vte *vte, struct csi *csi) { (void)vte, (void)csi; TODO("DECSACE"); return false; }

bool DECFRA(struct vte *vte, struct csi *csi) { (void)vte, (void)csi; TODO("DECFRA"); return false; }

bool DECRQCRA(struct vte *vte, struct csi *csi) { (void)vte, (void)csi; TODO("DECRQCRA"); return false; }

bool DECELR(struct vte *vte, struct csi *csi) { (void)vte, (void)csi; TODO("DECELR"); return false; }

bool DECERA(struct vte *vte, struct csi *csi) { (void)vte, (void)csi; TODO("DECERA"); return false; }

bool DECSLE(struct vte *vte, struct csi *csi) { (void)vte, (void)csi; TODO("DECSLE"); return false; }

bool DECSERA(struct vte *vte, struct csi *csi) { (void)vte, (void)csi; TODO("DECSERA"); return false; }

bool DECSCPP(struct vte *vte, struct csi *csi) { (void)vte, (void)csi; TODO("DECSCPP"); return false; }

bool DECRQLP(struct vte *vte, struct csi *csi) { (void)vte, (void)csi; TODO("DECRQLP"); return false; }

bool DECSNLS(struct vte *vte, struct csi *csi) { (void)vte, (void)csi; TODO("DECSNLS"); return false; }

bool DECAC(struct vte *vte, struct csi *csi) { (void)vte, (void)csi; TODO("DECAC"); return false; }

bool DECATC(struct vte *vte, struct csi *csi) { (void)vte, (void)csi; TODO("DECATC"); return false; }

bool DECIC(struct vte *vte, struct csi *csi) { (void)vte, (void)csi; TODO("DECIC"); return false; }

bool DECSASD(struct vte *vte, struct csi *csi) { (void)vte, (void)csi; TODO("DECSASD"); return false; }

bool DECDC(struct vte *vte, struct csi *csi) { (void)vte, (void)csi; TODO("DECDC"); return false; }

bool DECSSDT(struct vte *vte, struct csi *csi) { (void)vte, (void)csi; TODO("DECSSDT"); return false; }

