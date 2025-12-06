#include "utils.h"
#include "virtual_terminal_sequences.h"
#include "vte.h"
#include "vte_host.h"
#include <multiplexer.h>
#include <string.h>
#include <sys/wait.h>

static int nmaster = 1;
static float factor = 0.5;
void multiplexer_arrange(struct multiplexer *m) {
  struct {
    int ws_col, ws_row;
  } ws = {.ws_col = m->ws.colums, .ws_row = m->ws.rows};
  int i, n;
  int mh, mx, mw, my, sy, sw, nm, ns;
  int pixels_per_column = (int)((float)m->ws.y_pixel / (float)m->ws.colums);
  int pixels_per_row = (int)((float)m->ws.x_pixel / (float)m->ws.rows);

  n = m->hosts.length;
  struct vte_host *c;
  vec_foreach(c, m->hosts) c->border_width = 1;

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
    struct vte_host *p = vec_nth(&m->hosts, i);
    struct bounds b = {.x = mx, .y = my, .w = mw, .h = mh};
    b.x_pixel = b.w * pixels_per_column;
    b.y_pixel = b.h * pixels_per_row;
    vte_host_resize(p, b);
    my += mh;
  }

  int stack_height_left = ws.ws_row;
  int stack_items_left = ns;

  for (; i < n; i++) {
    struct vte_host *p = vec_nth(&m->hosts, i);
    int height = (float)stack_height_left / stack_items_left;
    struct bounds b = {.x = mw, .y = sy, .w = sw, .h = height};
    b.x_pixel = b.w * pixels_per_column;
    b.y_pixel = b.h * pixels_per_row;
    vte_host_resize(p, b);
    sy += height;
    stack_items_left--;
    stack_height_left -= height;
  }
}

#ifndef CTRL
#define CTRL(x) ((x) & 037)
#endif

static void vte_host_invalidate(struct vte_host *h) {
  h->border_dirty = true;
  vte_invalidate_screen(&h->vte);
}

static void multiplexer_swap_clients(struct multiplexer *m, size_t c1, size_t c2) {
  if (c1 != c2) {
    vec_swap(&m->hosts, c1, c2);
    vte_host_invalidate(vec_nth(&m->hosts, c1));
    vte_host_invalidate(vec_nth(&m->hosts, c2));
  }
}

static void host_notify_focus(struct vte_host *host, bool focus) {
  if (host->pty && host->vte.options.focus_reporting) {
    string_push_slice(&host->vte.pending_input, focus ? vt_focus_in : vt_focus_out);
  }
}

void multiplexer_set_focus(struct multiplexer *m, size_t focus) {
  if (m->focus != focus) {
    struct vte_host *current_focus = vec_nth(&m->hosts, m->focus);
    struct vte_host *new_focus = vec_nth(&m->hosts, focus);
    vte_host_invalidate(current_focus);
    vte_host_invalidate(new_focus);
    m->focus = focus;
    host_notify_focus(current_focus, false);
    host_notify_focus(new_focus, true);
  }
}

static void multiplexer_swap_previous(struct multiplexer *m) {
  if (m->focus > 0 && m->hosts.length > 1) {
    multiplexer_swap_clients(m, m->focus, m->focus - 1);
    multiplexer_set_focus(m, m->focus - 1);
  }
}

static void multiplexer_swap_next(struct multiplexer *m) {
  if (m->focus < m->hosts.length - 1 && m->hosts.length > 1) {
    multiplexer_swap_clients(m, m->focus, m->focus + 1);
    multiplexer_set_focus(m, m->focus + 1);
  }
}

static void multiplexer_focus_next(struct multiplexer *m) {
  multiplexer_set_focus(m, (m->focus + 1) % m->hosts.length);
}

static void multiplexer_focus_previous(struct multiplexer *m) {
  multiplexer_set_focus(m, (m->focus + m->hosts.length - 1) % m->hosts.length);
}

static void multiplexer_zoom(struct multiplexer *m) {
  if (m->hosts.length < 2) return;
  if (m->focus == 0) {
    multiplexer_swap_clients(m, 0, 1);
    multiplexer_set_focus(m, 0);
  } else {
    multiplexer_swap_clients(m, 0, m->focus);
    multiplexer_set_focus(m, 0);
  }
}

static void multiplexer_spawn_and_focus(struct multiplexer *m, char *cmdline) {
  multiplexer_spawn_process(m, cmdline);
  multiplexer_set_focus(m, m->hosts.length - 1);
}

static bool handle_keybinds(struct multiplexer *m, uint8_t ch) {
  switch (ch) {
  case 'c': multiplexer_spawn_and_focus(m, "zsh"); break;
  case 'j': multiplexer_swap_next(m); break;
  case 'k': multiplexer_swap_previous(m); break;
  case 'v': multiplexer_spawn_and_focus(m, "nvim"); break;
  case CTRL('q'): nmaster = MIN(10, nmaster + 1); break;
  case CTRL('e'): nmaster = MAX(0, nmaster - 1); break;
  case CTRL('g'): multiplexer_zoom(m); break;
  case CTRL('h'): factor = MAX(0.1f, factor - 0.05f); break;
  case CTRL('j'): multiplexer_focus_next(m); break;
  case CTRL('k'): multiplexer_focus_previous(m); break;
  case CTRL('l'): factor = MIN(0.9f, factor + 0.05f); break;
  default: return false;
  };
  return true;
}

void multiplexer_feed_input(struct multiplexer *m, struct u8_slice str) {
  const char ESC = 0x1b;
  static struct string writebuffer = {0};
  static struct string pastebuffer = {0};
  static enum stdin_state {
    normal,
    esc,
    csi,
    prefix,
    prefix_cont,
    bracketed_paste,
  } s;

  static const struct running_hash paste_start = {.characters = "\x1b[200~"};
  static const struct running_hash paste_end = {.characters = "\x1b[201~"};
  static struct running_hash running_hash = {0};

  string_clear(&writebuffer);

  for (size_t i = 0; i < str.len; i++) {
    struct vte_host *focused = vec_nth(&m->hosts, m->focus);
    uint8_t ch = str.content[i];
    running_hash_append(&running_hash, ch);
    if (s == normal && running_hash_match(running_hash, paste_start, 6)) {
      writebuffer.len -= 5;
      s = bracketed_paste;
      continue;
    } else if (s == bracketed_paste && running_hash_match(running_hash, paste_end, 6)) {
      pastebuffer.len -= 5;
      s = normal;
      if (focused && focused->vte.options.bracketed_paste) {
        string_push(&writebuffer, paste_start.characters);
        string_push_range(&writebuffer, pastebuffer.content, pastebuffer.len);
        string_push(&writebuffer, paste_end.characters);
      } else {
        string_push_range(&writebuffer, pastebuffer.content, pastebuffer.len);
      }
      string_clear(&pastebuffer);
      continue;
    }
  regret:
    switch (s) {
    case bracketed_paste: string_push_char(&pastebuffer, ch); break;
    case normal: {
      if (ch == ESC) {
        s = esc;
      } else if (ch == m->prefix) {
        s = prefix;
      } else {
        string_push_char(&writebuffer, ch);
      }
    } break;
    case esc: {
      if (ch == '[') {
        s = csi;
      } else {
        // escape next char
        string_push_char(&writebuffer, ESC);
        string_push_char(&writebuffer, ch);
        s = normal;
      }
    } break;
    case csi: {
      if (ch >= 'A' && ch <= 'D' && focused->vte.options.application_mode) {
        string_push_char(&writebuffer, ESC);
        string_push_char(&writebuffer, 'O');
        string_push_char(&writebuffer, ch);
      } else if (ch == 'O' || ch == 'I') {
        // Focus event. Forward it if the focused vte_host has the feature enabled
        bool did_focus = ch == 'I';
        if (focused->vte.options.focus_reporting) {
          if (did_focus)
            string_push_slice(&writebuffer, vt_focus_in);
          else
            string_push_slice(&writebuffer, vt_focus_out);
        }
        // If the vte_host does not have the feature enabled, ignore it.
      } else {
        string_push_csi(&writebuffer, 0, (struct int_slice){0}, (uint8_t[]){ch, 0});
      }
      s = normal;
    } break;
    case prefix_cont: {
      bool is_control = CTRL(ch) == ch;
      if (!is_control) {
        s = normal;
        goto regret;
      }
      if (ch == m->prefix) {
        s = prefix;
        continue;
      }
      if (!handle_keybinds(m, ch)) {
        s = normal;
        goto regret;
      }
    } break;
    case prefix: {
      bool is_control = CTRL(ch) == ch;
      if (ch == m->prefix) {
        string_push_char(&writebuffer, m->prefix);
        s = normal;
      } else {
        handle_keybinds(m, ch);
        s = is_control ? prefix_cont : normal;
      }
    } break;
    }
  }
  struct vte_host *focused = vec_nth(&m->hosts, m->focus);

  // TODO: Implement timing mechanism for escapes.
  // For now, flush before return to restore state machine to normal input mode
  if (s == csi) {
    string_push_char(&writebuffer, ESC);
    string_push_char(&writebuffer, '[');
    s = normal;
  } else if (s == esc) {
    string_push_char(&writebuffer, ESC);
    s = normal;
  }

  // TODO: Push this to focused->vte->pending_output instead?
  // To consolidate pty writing and avoid blocking the main loop / other clients
  // in cases where a single client is slow
  if (writebuffer.len) {
    string_flush(&writebuffer, focused->pty, nullptr);
  }
}

void multiplexer_spawn_process(struct multiplexer *m, char *process) {
  assert(m->hosts.element_size == sizeof(struct vte_host));
  logmsg("Spawn %s", process);
  struct vte_host *host = vec_new_element(&m->hosts);
  host->cmdline = strdup(process);
  host->vte = vte_default;
  multiplexer_arrange(m);
  vte_host_start(host);
}

void multiplexer_destroy(struct multiplexer *m) {
  struct vte_host *h;
  vec_foreach(h, m->hosts) {
    vte_host_destroy(h);
  }
  vec_destroy(&m->hosts);
  string_destroy(&m->draw_buffer);
}

static void multiplexer_remove_host(struct multiplexer *m, size_t index) {
  vec_remove(&m->hosts, index);

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
    struct vte_host *new_focus = vec_nth(&m->hosts, next_focus);
    host_notify_focus(new_focus, true);
  }
}

void multiplexer_remove_exited(struct multiplexer *m) {
  int status;
  pid_t pid;

  while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
    if (WIFEXITED(status)) {
      struct vte_host *h;
      vec_foreach(h, m->hosts) {
        if (h->pid == pid) {
          // zero the pid to indicate the process exited.
          // otherwise the process will be reaped in vte_host_destroy
          h->pid = 0;
          vte_host_destroy(h);
          multiplexer_remove_host(m, vec_index(h, m->hosts));
          break;
        }
      }
    }
  }
}

void multiplexer_resize(struct multiplexer *m, struct platform_winsize w) {
  if (m->ws.colums != w.colums || m->ws.rows != w.rows || m->ws.x_pixel != w.x_pixel || m->ws.y_pixel != w.y_pixel) {
    m->ws = w;
    multiplexer_arrange(m);
    struct vte_host *h;
    vec_foreach(h, m->hosts) {
      vte_host_invalidate(h);
    }
  }
}

void multiplexer_render(struct multiplexer *m, render_func_t *render_func, bool full_draw, void *context) {
  if (m->hosts.length == 0) return;
  static enum cursor_style current_cursor_style = 0;
  struct string *draw_buffer = &m->draw_buffer;
  struct vte_host *focused = vec_nth(&m->hosts, m->focus);

  if (m->host_features.synchronized_rendering) {
    // if the host supports synchronized rendering, make use of it to ensure
    // the full frame is written before it is rendered.
    string_push_slice(draw_buffer, vt_synchronized_rendering_on);
  } else {
    // if the host does not advertise support, hide the cursor while drawing instead
    string_push_slice(draw_buffer, vt_cursor_visible_off);
  }
  for (size_t i = 0; i < m->hosts.length; i++) {
    struct vte_host *h = vec_nth(&m->hosts, i);
    vte_host_update_cwd(h);
    vte_host_draw(h, full_draw, draw_buffer);
    bool stored = h->border_dirty;
    if (full_draw) h->border_dirty = true;
    vte_host_draw_border(h, draw_buffer, i == m->focus);
    h->border_dirty = stored;
  }

  {
    // move cursor to focused host
    struct screen *g = vte_get_current_screen(&focused->vte);
    struct cursor *c = &g->cursor;
    int lineno = 1 + focused->rect.client.y + c->row;
    int columnno = 1 + focused->rect.client.x + c->column;
    string_push_csi(draw_buffer, 0, INT_SLICE(lineno, columnno), "H");
  }

  // set the cursor style according to the focused client.
  if (focused->vte.options.cursor.style != current_cursor_style) {
    current_cursor_style = focused->vte.options.cursor.style;
    string_push_csi(draw_buffer, 0, INT_SLICE(current_cursor_style), " q");
  }

  // Set cursor visibility according to the focused client.
  if (focused->vte.options.cursor.visible) string_push_slice(draw_buffer, vt_cursor_visible_on);
  if (m->host_features.synchronized_rendering) string_push_slice(draw_buffer, vt_synchronized_rendering_off);

  struct u8_slice s = {.content = m->draw_buffer.content, .len = m->draw_buffer.len};
  render_func(s, context);
  string_clear(&m->draw_buffer);
}
