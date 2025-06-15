#include "emulator.h"
#include "collections.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NUL 0
#define BELL '\a'
#define BSP '\b'
#define DEL 0x7f
#define ESC 0x1b
#define FORMFEED '\f'
#define NEWLINE '\n'
#define RET '\r'
#define TAB '\t'
#define VTAB '\v'
#define CSI '['
#define DCS 'P'
#define OSC ']'

static void send_escape(struct fsm *f) {
  assert(f->seq.n);
  fwrite(f->seq.buffer, 1, f->seq.n, stdout);
  f->seq.n = 0;
}

static void process_csi(struct fsm *fsm, unsigned char ch) {
  fsm->seq.buffer[fsm->seq.n++] = ch;
  if (ch >= 0x40 && ch <= 0x7E) {
    // terminal -- parse the command
    fsm->state = fsm_ground;
  } else if (fsm->seq.n >= MAX_ESC_SEQ_LEN) {
    fsm->state = fsm_ground;
  }
  // switch (ch) {
  // case 's': { /* save cursor position */
  //   fsm->state = fsm_ground;
  //   fsm->saved_cursor = fsm->cur;
  // } break;
  // case 'u': { /* restore saved cursor position */
  //   fsm->state = fsm_ground;
  //   fsm->cur = fsm->saved_cursor;
  // } break;
  // default: {
  // } break;
  // }
}

static void grid_destroy(struct grid *grid);
static void fix_grid(struct grid *g, int w, int h) {
  if (!g->cells) {
    g->cells = calloc(w * h, sizeof(*g->cells));
    g->dirty = calloc(w * h, sizeof(*g->dirty));
    g->h = h;
    g->w = w;
  }

  for (int i = 0; i < h; i++) {
    g->dirty[i] = false;
  }
  // TODO: Enlarge grids if too small
  // TODO: Reflow grids if dimensions changed
  if (g->h != h || g->w != w) {
    struct grid new = {.w = w, .h = h};
    fix_grid(&new, w, h);
    // TODO: Iterate all cells from `g' and insert them in `new'
    // Insertion function should reflow
    grid_destroy(g);
    *g = new;
  }
}

static void fix_grids(struct fsm *fsm) {
  fix_grid(&fsm->primary, fsm->w, fsm->h);
  fix_grid(&fsm->alternate, fsm->w, fsm->h);
}

struct grid *fsm_active_grid(struct fsm *fsm) {
  return fsm->opts.alternate_screen ? &fsm->alternate : &fsm->primary;
}

static void grid_insert(struct grid *g, struct cell c) {
  /* Implementation notes:
   * 1. The width of a cell depends on the codepoints
   * 1. If auto-wrapping is disabled, and a double width character is inserted in the last column, it should be
   * rejected.
   * */
}

static void ground_esc(struct fsm *fsm, uint8_t ch) {
  fsm->state = fsm_escape;
  fsm->seq.n = 0;
  fsm->seq.buffer[fsm->seq.n++] = ch;
}
static void ground_noop(struct fsm *fsm, uint8_t ch) {
  /* ignored */
}
static void ground_carriage_return(struct fsm *fsm, uint8_t ch) {
  fsm->cur.x = 0;
}
static void ground_backspace(struct fsm *fsm, uint8_t ch) {
  // Move cursor back, but not past the first column
  fsm->cur.x = MAX(0, fsm->cur.x - 1);
}
static void ground_vtab(struct fsm *fsm, uint8_t ch) {
  // Ignore wrapping rules and preserve column
  fsm->cur.y++;
}
static void ground_tab(struct fsm *fsm, uint8_t ch) {
  const int tabwidth = 8;
  int x = fsm->cur.x;
  x = ((x / tabwidth) + 1) * tabwidth;
  if (x >= fsm->w) {
    fsm->cur.x = 0;
    fsm->cur.y++;
  } else {
    fsm->cur.x = x;
  }
}

static void ground_bell(struct fsm *fsm, uint8_t ch) {
  write(STDOUT_FILENO, "\a", 1);
}

static void ground_newline(struct fsm *fsm, uint8_t ch) {
  fsm->cur.y++;
  if (fsm->opts.auto_return) fsm->cur.x = 0;
}

static void ground_accept(struct fsm *fsm) {
}
static void ground_reject(struct fsm *fsm) {
  struct utf8 clear = {0};
  struct utf8 copy = fsm->current.ch;
  fsm->current.ch = clear;
  for (int i = 1; i < (int)(sizeof(copy.utf8)) && copy.utf8[i]; i++) {
  }
}

static void process_ground(struct fsm *fsm, uint8_t ch) {
  static void (*const dispatch[UINT8_MAX])(struct fsm *, uint8_t) = {
      [NUL] = ground_noop,        [ESC] = ground_esc,   [RET] = ground_carriage_return,
      [BSP] = ground_backspace,   [BELL] = ground_bell, [DEL] = ground_noop,
      [TAB] = ground_tab,         [VTAB] = ground_vtab, [FORMFEED] = ground_newline,
      [NEWLINE] = ground_newline,
  };

  if (dispatch[ch]) {
    dispatch[ch](fsm, ch);
    return;
  }
}

void fsm_process(struct fsm *fsm, unsigned char *buf, int n) {
  fix_grids(fsm);
  for (int i = 0; i < n; i++) {
    uint8_t ch = buf[i];
    switch (fsm->state) {
    case fsm_ground: {
      process_ground(fsm, ch);
    } break;
    case fsm_escape: {
      switch (ch) {
      case CSI: {
        fsm->state = fsm_csi;
        fsm->seq.buffer[fsm->seq.n++] = ch;
      } break;
      case OSC: {
        fsm->state = fsm_osc;
        fsm->seq.buffer[fsm->seq.n++] = ch;
      } break;
      case DCS: {
        fsm->state = fsm_dcs;
        fsm->seq.buffer[fsm->seq.n++] = ch;
      } break;
        /* legacy save/restore */
      case '7': {
        fsm->state = fsm_ground;
        fsm->saved_cursor = fsm->cur;
      } break;
      case '8': {
        fsm->state = fsm_ground;
        fsm->cur = fsm->saved_cursor;
      } break;
      default: {
        // Unrecognized escape. Treat this char as escaped and continue parsing normally.
        fsm->state = fsm_ground;
        break;
      }
      }
    } break;
    case fsm_dcs: {
      fsm->seq.buffer[fsm->seq.n++] = ch;
      if (ch == '\\' && fsm->seq.buffer[fsm->seq.n] == ESC) {
        fsm->state = fsm_ground;
      } else if (fsm->seq.n >= MAX_ESC_SEQ_LEN) {
        fsm->state = fsm_ground;
      }
    } break;
    case fsm_osc: {
      fsm->seq.buffer[fsm->seq.n++] = ch;
      if (ch == BELL) {
        send_escape(fsm);
        fsm->state = fsm_ground;
      } else if (fsm->seq.n >= MAX_ESC_SEQ_LEN) {
        fsm->state = fsm_ground;
      }
    } break;
    case fsm_csi: {
      process_csi(fsm, ch);
    } break;
    default: {
      assert(!"Unreachable");
    }
    }
  }
}

void grid_destroy(struct grid *grid) {
  free(grid->cells);
  free(grid->dirty);
  grid->cells = NULL;
  grid->dirty = NULL;
}

void fsm_destroy(struct fsm *fsm) {
  grid_destroy(&fsm->primary);
  grid_destroy(&fsm->alternate);
}
