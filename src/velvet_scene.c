#include "utils.h"
#include "virtual_terminal_sequences.h"
#include "vte.h"
#include "pty_host.h"
#include <velvet_scene.h>
#include <string.h>
#include <sys/wait.h>

static int nmaster = 1;
static float factor = 0.5;
void velvet_scene_arrange(struct velvet_scene *m) {
  struct {
    int ws_col, ws_row;
  } ws = {.ws_col = m->ws.colums, .ws_row = m->ws.lines};
  int i, n;
  int mh, mx, mw, my, sy, sw, nm, ns;
  int pixels_per_column = (int)((float)m->ws.y_pixel / (float)m->ws.colums);
  int pixels_per_row = (int)((float)m->ws.x_pixel / (float)m->ws.lines);

  n = m->hosts.length;

  i = my = sy = mx = 0;
  nm = n > nmaster ? nmaster : n;
  ns = n > nmaster ? n - nmaster : 0;

  mh = (int)((float)ws.ws_row / (float)nm);

  if (nmaster <= 0) {
    mw = 0;
    sw = ws.ws_col;
  } else if (n > nmaster) {
    mw = (int)((float)ws.ws_col * factor);
    sw = ws.ws_col - mw;
  } else {
    mw = ws.ws_col;
    sw = 0;
  }

  for (; i < nmaster && i < n; i++) {
    struct pty_host *p = vec_nth(&m->hosts, i);
    struct bounds b = {.x = mx, .y = my, .columns = mw, .lines = mh};
    b.x_pixel = b.columns * pixels_per_column;
    b.y_pixel = b.lines * pixels_per_row;
    pty_host_resize(p, b);
    my += mh;
  }

  int stack_height_left = ws.ws_row;
  int stack_items_left = ns;

  for (; i < n; i++) {
    struct pty_host *p = vec_nth(&m->hosts, i);
    int height = (float)stack_height_left / stack_items_left;
    struct bounds b = {.x = mw, .y = sy, .columns = sw, .lines = height};
    b.x_pixel = b.columns * pixels_per_column;
    b.y_pixel = b.lines * pixels_per_row;
    pty_host_resize(p, b);
    sy += height;
    stack_items_left--;
    stack_height_left -= height;
  }
}

#ifndef CTRL
#define CTRL(x) ((x) & 037)
#endif

static void pty_host_invalidate(struct pty_host *h) {
  vte_invalidate_screen(&h->emulator);
}

static void velvet_scene_swap_clients(struct velvet_scene *m, size_t c1, size_t c2) {
  if (c1 != c2) {
    vec_swap(&m->hosts, c1, c2);
    pty_host_invalidate(vec_nth(&m->hosts, c1));
    pty_host_invalidate(vec_nth(&m->hosts, c2));
  }
}

static void host_notify_focus(struct pty_host *host, bool focus) {
  if (host->pty && host->emulator.options.focus_reporting) {
    string_push_slice(&host->emulator.pending_input, focus ? vt_focus_in : vt_focus_out);
  }
}

void velvet_scene_set_focus(struct velvet_scene *m, size_t focus) {
  if (m->focus != focus) {
    struct pty_host *current_focus = vec_nth(&m->hosts, m->focus);
    struct pty_host *new_focus = vec_nth(&m->hosts, focus);
    pty_host_invalidate(current_focus);
    pty_host_invalidate(new_focus);
    m->focus = focus;
    host_notify_focus(current_focus, false);
    host_notify_focus(new_focus, true);
  }
}

static void velvet_scene_swap_previous(struct velvet_scene *m) {
  if (m->focus > 0 && m->hosts.length > 1) {
    velvet_scene_swap_clients(m, m->focus, m->focus - 1);
    velvet_scene_set_focus(m, m->focus - 1);
  }
}

static void velvet_scene_swap_next(struct velvet_scene *m) {
  if (m->focus < m->hosts.length - 1 && m->hosts.length > 1) {
    velvet_scene_swap_clients(m, m->focus, m->focus + 1);
    velvet_scene_set_focus(m, m->focus + 1);
  }
}

static void velvet_scene_focus_next(struct velvet_scene *m) {
  velvet_scene_set_focus(m, (m->focus + 1) % m->hosts.length);
}

static void velvet_scene_focus_previous(struct velvet_scene *m) {
  velvet_scene_set_focus(m, (m->focus + m->hosts.length - 1) % m->hosts.length);
}

static void velvet_scene_zoom(struct velvet_scene *m) {
  if (m->hosts.length < 2) return;
  if (m->focus == 0) {
    velvet_scene_swap_clients(m, 0, 1);
    velvet_scene_set_focus(m, 0);
  } else {
    velvet_scene_swap_clients(m, 0, m->focus);
    velvet_scene_set_focus(m, 0);
  }
}

static void velvet_scene_spawn_and_focus(struct velvet_scene *m, char *cmdline) {
  velvet_scene_spawn_process(m, u8_slice_from_cstr(cmdline));
  velvet_scene_set_focus(m, m->hosts.length - 1);
}

void velvet_scene_spawn_process(struct velvet_scene *m, struct u8_slice cmdline) {
  assert(m->hosts.element_size == sizeof(struct pty_host));
  struct pty_host *host = vec_new_element(&m->hosts);
  string_clear(&host->cmdline);
  string_push_slice(&host->cmdline, cmdline);
  host->emulator = vte_default;
  host->border_width = 1;
  velvet_scene_arrange(m);
  pty_host_start(host);
}

static void velvet_scene_renderer_destroy(struct velvet_scene_renderer *renderer) {
  string_destroy(&renderer->draw_buffer);
  free(renderer->cells);
  free(renderer->lines);
}

void velvet_scene_destroy(struct velvet_scene *m) {
  struct pty_host *h;
  vec_foreach(h, m->hosts) {
    pty_host_destroy(h);
  }
  vec_destroy(&m->hosts);
  velvet_scene_renderer_destroy(&m->renderer);
}

static void velvet_scene_remove_host(struct velvet_scene *m, size_t index) {
  vec_remove_at(&m->hosts, index);

  if (m->hosts.length == 0) return;

  // Update focus
  if (m->focus > index) {
    // if the focus was after the removed index, decrement it.
    m->focus -= 1;
  } else if (m->focus == index) {
    // if the removed host was focused, keep the same focus index if possible.
    size_t next_focus = index;
    // If the removed host was the last host, set the focus to the new last host.
    if (next_focus >= m->hosts.length)
      next_focus = m->hosts.length - 1;
    m->focus = next_focus;
    struct pty_host *new_focus = vec_nth(&m->hosts, next_focus);
    host_notify_focus(new_focus, true);
  }
}

void velvet_scene_remove_exited(struct velvet_scene *m) {
  int status;
  pid_t pid = 0;

  while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
    struct pty_host *h;
    vec_find(h, m->hosts, h->pid == pid);
    if (!h) continue;
    h->pid = 0;
    pty_host_destroy(h);
    velvet_scene_remove_host(m, vec_index(&m->hosts, h));
  }
}

void velvet_scene_resize(struct velvet_scene *m, struct platform_winsize w) {
  if (m->ws.colums != w.colums || m->ws.lines != w.lines || m->ws.x_pixel != w.x_pixel || m->ws.y_pixel != w.y_pixel) {
    m->ws = w;
    velvet_scene_arrange(m);
    struct pty_host *h;
    vec_foreach(h, m->hosts) {
      pty_host_invalidate(h);
    }
  }
}

static void velvet_scene_renderer_set_cell(struct velvet_scene_renderer *r, int line, int column, struct screen_cell value) {
  struct velvet_scene_renderer_line *l = &r->lines[line];
  int index = l->cell_offset + column;
  if (!cell_equals(r->cells[index], value)) {
    r->cells[index] = value;
    l->damage.start = MIN(l->damage.start, column);
    l->damage.end = MAX(l->damage.end, column);
  }
}

static void velvet_scene_renderer_set_cursor_style(struct velvet_scene_renderer *r, struct cursor_options cursor) {
  if (r->current_cursor.style != cursor.style) {
    string_push_csi(&r->draw_buffer, 0, INT_SLICE(cursor.style), " q");
  }
  if (r->current_cursor.visible != cursor.visible) {
    string_push_slice(&r->draw_buffer, cursor.visible ? vt_cursor_visible_on : vt_cursor_visible_off);
  }
  r->current_cursor = cursor;
}

static void velvet_scene_renderer_set_style(struct velvet_scene_renderer *r, struct screen_cell_style style);

static void velvet_scene_renderer_position_cursor(struct velvet_scene_renderer *r, int line, int col) {
  if (r->cursor.line != line && r->cursor.column != col) {
    string_push_csi(&r->draw_buffer, 0, INT_SLICE(line + 1, col + 1), "H");
  } else if (r->cursor.line != line) {
    string_push_csi(&r->draw_buffer, 0, INT_SLICE(line + 1), "d");
  } else if (r->cursor.column != col) {
    string_push_csi(&r->draw_buffer, 0, INT_SLICE(col + 1), "G");
  }

  r->cursor.column = col;
  r->cursor.line = line;
}

static void velvet_scene_renderer_draw_to_buffer(struct velvet_scene_renderer *r) {
  int ll, lc;
  ll = lc = -1;

  for (int line = 0; line < r->h; line++) {
    struct velvet_scene_renderer_line *l = &r->lines[line];
    if (l->damage.start >= r->w) continue;

    velvet_scene_renderer_position_cursor(r, line, l->damage.start);
    for (int col = l->damage.start; col <= l->damage.end; col++) {
      struct screen_cell *c = &r->cells[l->cell_offset + col];
      velvet_scene_renderer_set_style(r, c->style);
      if (c->symbol.numeric == 0) c->symbol.numeric = ' ';

      uint8_t utf8_len = 0;
      struct utf8 sym = c->symbol;
      for (; utf8_len < 4 && sym.utf8[utf8_len]; utf8_len++);
      struct u8_slice text = { .content = sym.utf8, .len = utf8_len };
      string_push_slice(&r->draw_buffer, text);

      int repeats = 1;
      int remaining = l->damage.end - col - 1;
      for (; repeats < remaining && cell_equals(c[0], c[repeats]); repeats++);
      repeats--;
      if (repeats > 0) {
        int num_bytes = utf8_len * repeats;
        bool can_repeat = utf8_len == 1 || r->options.no_repeat_wide_chars == false;
        if (num_bytes > 10 && can_repeat) {
          string_push_csi(&r->draw_buffer, 0, INT_SLICE(repeats), "b");
        } else {
          for (int i = 0; i < repeats; i++) {
            string_push_slice(&r->draw_buffer, text);
          }
        }
        col += repeats;
      }

      ll = line;
      lc = col + 1;
      if (lc >= r->w) lc = r->w - 1;
      r->cursor.column = lc;
      r->cursor.line = ll;
    }
  }
}

static void velvet_scene_renderer_draw_cells(struct velvet_scene_renderer *r, struct velvet_scene *m) {
  struct pty_host *h;
  vec_foreach(h, m->hosts) {
    struct screen *active = vte_get_current_screen(&h->emulator);
    assert(active);
    assert(active->w == h->rect.client.columns);
    assert(active->h == h->rect.client.lines);
    for (int line = 0; line < active->h; line++) {
      struct screen_line *screen_line = &active->lines[line];
      int render_line = h->rect.client.y + line;
      if (render_line >= r->h) break;
      for (int column = 0; column < active->w; column++) {
        int render_column = h->rect.client.x + column;
        if (render_column >= r->w) break;
        velvet_scene_renderer_set_cell(r, render_line, render_column, screen_line->cells[column]);
      }
    }
  }
}

/* lol */
static struct utf8 utf8_from_cstr(char *src) {
  struct utf8 result = {0};
  char *dst = (char*)result.utf8;
  for (; *src; *dst++ = *src++);
  return result;
}

static void velvet_scene_renderer_draw_borders(struct velvet_scene_renderer *r, struct velvet_scene *m) {
  struct utf8 topleft = utf8_from_cstr("┌");
  struct utf8 topright = utf8_from_cstr("┐");
  struct utf8 bottomleft = utf8_from_cstr("└");
  struct utf8 bottomright = utf8_from_cstr("┘");
  struct utf8 pipe = utf8_from_cstr("│");
  struct utf8 dash = utf8_from_cstr("─");
  struct utf8 top_connector = utf8_from_cstr("┬");
  struct utf8 left_connector = utf8_from_cstr("├");
  struct utf8 right_connector = utf8_from_cstr("┤");
  struct utf8 cross_connector = utf8_from_cstr("┼");
  struct utf8 elipsis = utf8_from_cstr("…");

  struct pty_host *host;
  vec_foreach(host, m->hosts) {
    pty_host_update_cwd(host);
  }

  /* draw title bars for each window */
  vec_where(host, m->hosts, host->border_width) {
    bool is_focused = (int)m->focus == vec_index(&m->hosts, host);
    int i, x, y, h, w;
    i = 0;
    w = host->rect.window.columns; h = host->rect.window.lines; x = host->rect.window.x; y = host->rect.window.y;

    struct screen_cell_style chrome_style = is_focused ? m->style.active.outline : m->style.inactive.outline;
    struct screen_cell_style title_style = is_focused ? m->style.active.title : m->style.inactive.title;
    struct screen_cell chrome = {.style = chrome_style, .symbol = dash};

    struct screen_cell space = { .symbol.numeric = ' ', .style = title_style };
    struct screen_cell truncation_symbol = { .symbol = elipsis, .style = title_style };

    /* dashes before the window title */
    for (; i < 2; i++) velvet_scene_renderer_set_cell(r, y, x + i, chrome);
    velvet_scene_renderer_set_cell(r, y, x + i++, space);

    /* draw each codepoint of the title */
    struct u8_slice_codepoint_iterator it = {.src = string_as_u8_slice(host->title)};
    for (; i < w - 2 && u8_slice_codepoint_iterator_next(&it); i++) {
      struct screen_cell chr = {.style = title_style};
      if (it.invalid) {
        chr.symbol = utf8_fffd;
      } else {
        memcpy(chr.symbol.utf8, it.current.content, it.current.len);
      }
      velvet_scene_renderer_set_cell(r, y, x + i, chr);
    }

    /* add a space or truncation symbol */
    if (u8_slice_codepoint_iterator_next(&it)) {
      /* title was truncated */
      velvet_scene_renderer_set_cell(r, y, x + i++, truncation_symbol);
    } else {
      velvet_scene_renderer_set_cell(r, y, x + i++, space);
    }

    /* draw the rest of the line */
    for (; i < w; i++) velvet_scene_renderer_set_cell(r, y, x + i, chrome);
  }

  /* now we need to detect the edges between windows */
}

void velvet_scene_render_full(struct velvet_scene *m, render_func_t *render_func, void *context) {
  if (m->hosts.length == 0) return;
  struct pty_host *focused = vec_nth(&m->hosts, m->focus);
  struct velvet_scene_renderer *r = &m->renderer;
  string_clear(&r->draw_buffer);

  /* we don't know where the cursor could be, so ensure it is correctly moved when needed */
  r->cursor.line = -1;
  r->cursor.column = -1;

  /* Mark each line as fully damaged. */
  for (int i = 0; i < r->h; i++) {
    struct velvet_scene_renderer_line *l = &r->lines[i];
    l->damage.end = r->w - 1;
    l->damage.start = 0;
  }

  string_push_cstr(&r->draw_buffer, "\x1b[2J");
  velvet_scene_renderer_draw_to_buffer(r);

  /* move cursor to focused host */
  struct screen *screen = vte_get_current_screen(&focused->emulator);
  struct cursor *cursor = &screen->cursor;

  velvet_scene_renderer_position_cursor(
      r, cursor->line + focused->rect.client.y, cursor->column + focused->rect.client.x);
  velvet_scene_renderer_set_cursor_style(r, focused->emulator.options.cursor);

  struct u8_slice render = string_as_u8_slice(r->draw_buffer);
  render_func(render, context);
}

// TODO: Invalidate renderer when any pty client is resized or moved */
void velvet_scene_render_damage(struct velvet_scene *m, render_func_t *render_func, void *context) {
  if (m->hosts.length == 0) return;
  struct pty_host *focused = vec_nth(&m->hosts, m->focus);

  struct velvet_scene_renderer *r = &m->renderer;
  string_clear(&r->draw_buffer);

  if (!r->cells || r->h != m->ws.lines || r->w != m->ws.colums) {
    free(r->cells);
    free(r->lines);
    r->h = m->ws.lines; r->w = m->ws.colums;
    r->cells = velvet_calloc(sizeof(*r->cells), r->h * r->w);
    r->lines = velvet_calloc(sizeof(*r->lines), r->h);
    for (int i = 0; i < r->h; i++) {
      r->lines[i].cell_offset = r->w * i;
    }
    string_push_cstr(&r->draw_buffer, "\x1b[2J");
  }

  /* Mark each line as undamaged. */
  for (int i = 0; i < r->h; i++) {
    struct velvet_scene_renderer_line *l = &r->lines[i];
    l->damage.end = -1;
    l->damage.start = r->w;
  }

  velvet_scene_renderer_draw_cells(r, m);
  velvet_scene_renderer_draw_borders(r, m);
  velvet_scene_renderer_draw_to_buffer(r);

  /* move cursor to focused host */
  struct screen *screen = vte_get_current_screen(&focused->emulator);
  struct cursor *cursor = &screen->cursor;

  velvet_scene_renderer_position_cursor(r, cursor->line + focused->rect.client.y, cursor->column + focused->rect.client.x);
  velvet_scene_renderer_set_cursor_style(r, focused->emulator.options.cursor);

  struct u8_slice render = string_as_u8_slice(r->draw_buffer);
  render_func(render, context);
}

struct sgr_param {
  uint8_t primary;
  uint8_t sub[4];
  int n_sub;
};

// Terminal emulators may not have infinite sapce set aside for SGR sequences,
// so let's split our sequences at this threshold. Such sequences should be unusual anyway.
#define MAX_LOAD 10
#define SGR_PARAMS_MAX 12
struct sgr_buffer {
  struct sgr_param params[SGR_PARAMS_MAX];
  uint8_t n;
};

static inline void sgr_buffer_push(struct sgr_buffer *b, int n) {
  assert(b->n < SGR_PARAMS_MAX);
  b->params[b->n] = (struct sgr_param){.primary = n};
  b->n++;
}
static inline void sgr_buffer_add_param(struct sgr_buffer *b, int sub) {
  assert(b->n);
  int k = b->n - 1;
  struct sgr_param *p = &b->params[k];
  p->sub[p->n_sub] = sub;
  p->n_sub++;
}

static void sgr_color_apply(struct sgr_buffer *sgr, struct color col, bool fg) {
  if (col.cmd == COLOR_RESET) {
    sgr_buffer_push(sgr, fg ? 39 : 49);
  } else if (col.cmd == COLOR_TABLE) {
    if (col.table <= 7) {
      sgr_buffer_push(sgr, (fg ? 30 : 40) + col.table);
    } else if (col.table <= 15) {
      sgr_buffer_push(sgr, (fg ? 90 : 100) + col.table - 8);
    } else {
      sgr_buffer_push(sgr, fg ? 38 : 48);
      sgr_buffer_add_param(sgr, 5);
      sgr_buffer_add_param(sgr, col.table);
    }
  } else if (col.cmd == COLOR_RGB) {
    sgr_buffer_push(sgr, fg ? 38 : 48);
    sgr_buffer_add_param(sgr, 2);
    sgr_buffer_add_param(sgr, col.r);
    sgr_buffer_add_param(sgr, col.g);
    sgr_buffer_add_param(sgr, col.b);
  }
}

static void velvet_scene_renderer_set_style(struct velvet_scene_renderer *r, struct screen_cell_style style) {
  struct color fg = r->current_style.fg;
  struct color bg = r->current_style.bg;
  uint32_t attr = r->current_style.attr;
  struct sgr_buffer sgr = {.n = 0 };

  // 1. Handle attributes
  if (attr != style.attr) {
    if (style.attr == 0) {
      // Unfortunately this also resets colors, so we need to set those again
      // Technically we could track what styles are active here and disable those specifically,
      // but it is much simpler to just eat the color reset.
      fg = bg = color_default;
      sgr_buffer_push(&sgr, 0);
    } else {
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
      for (size_t i = 1; i < LENGTH(features); i++) {
        uint32_t current = features[i] & attr;
        uint32_t next = features[i] & style.attr;
        if (current && !next) {
          if (i == 1) {
            // annoying special case for bold
            sgr_buffer_push(&sgr, 22);
          } else {
            sgr_buffer_push(&sgr, 20 + i);
          }
        } else if (!current && next) {
          sgr_buffer_push(&sgr, i);
        }
      }
    }
  }

  if (!color_equals(fg, style.fg)) {
    sgr_color_apply(&sgr, style.fg, true);
  }
  if (!color_equals(bg, style.bg)) {
    sgr_color_apply(&sgr, style.bg, false);
  }

  // struct screen_cell_style new_style = { .attr = attr, .bg = bg, .fg = fg };
  r->current_style = style;

  if (sgr.n) {
    struct string *w = &r->draw_buffer;
    string_push(w, u8"\x1b[");
    int current_load = 0;
    for (int i = 0; i < sgr.n; i++) {
      struct sgr_param *p = &sgr.params[i];
      int this_load = 1 + p->n_sub;
      if (current_load + this_load > MAX_LOAD) {
        w->content[w->len - 1] = 'm';
        string_push(w, u8"\x1b[");
        current_load = 0;
      }
      current_load += this_load;
      string_push_int(w, p->primary);
      for (int j = 0; j < p->n_sub; j++) {
        // I would prefer to properly use ':' to split subparameters here, but it appears that 
        // some terminal emulators do not properly implement this. For compatibility, 
        // use ';' as a separator instead.
        string_push_char(w, ';');
        string_push_int(w, p->sub[j]);
      }
      string_push_char(w, ';');
    }
    w->content[w->len - 1] = 'm';
  }
}

