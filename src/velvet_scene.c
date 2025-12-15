#include "utils.h"
#include "virtual_terminal_sequences.h"
#include "vte.h"
#include "vte_host.h"
#include <velvet_scene.h>
#include <string.h>
#include <sys/wait.h>

static int nmaster = 1;
static float factor = 0.5;
void velvet_scene_arrange(struct velvet_scene *m) {
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

static void velvet_scene_swap_clients(struct velvet_scene *m, size_t c1, size_t c2) {
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

void velvet_scene_set_focus(struct velvet_scene *m, size_t focus) {
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
  velvet_scene_spawn_process(m, cmdline);
  velvet_scene_set_focus(m, m->hosts.length - 1);
}

void velvet_scene_spawn_process(struct velvet_scene *m, char *process) {
  assert(m->hosts.element_size == sizeof(struct vte_host));
  velvet_log("Spawn %s", process);
  struct vte_host *host = vec_new_element(&m->hosts);
  host->cmdline = strdup(process);
  host->vte = vte_default;
  velvet_scene_arrange(m);
  vte_host_start(host);
}

void velvet_scene_destroy(struct velvet_scene *m) {
  struct vte_host *h;
  vec_foreach(h, m->hosts) {
    vte_host_destroy(h);
  }
  vec_destroy(&m->hosts);
  string_destroy(&m->draw_buffer);
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
    struct vte_host *new_focus = vec_nth(&m->hosts, next_focus);
    host_notify_focus(new_focus, true);
  }
}

void velvet_scene_remove_exited(struct velvet_scene *m) {
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
          velvet_scene_remove_host(m, vec_index(&m->hosts, h));
          break;
        }
      }
    }
  }
}

void velvet_scene_resize(struct velvet_scene *m, struct platform_winsize w) {
  if (m->ws.colums != w.colums || m->ws.rows != w.rows || m->ws.x_pixel != w.x_pixel || m->ws.y_pixel != w.y_pixel) {
    m->ws = w;
    velvet_scene_arrange(m);
    struct vte_host *h;
    vec_foreach(h, m->hosts) {
      vte_host_invalidate(h);
    }
  }
}

void velvet_scene_render(struct velvet_scene *m, render_func_t *render_func, bool full_draw, void *context) {
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
