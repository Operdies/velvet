#include "utils.h"
#include "virtual_terminal_sequences.h"
#include "vte.h"
#include "vte_host.h"
#include <multiplexer.h>
#include <string.h>

static int nmaster = 1;
static float factor = 0.5;
static void multiplexer_arrange(struct multiplexer *m) {
  struct {
    int ws_col, ws_row;
  } ws = {.ws_col = m->columns, .ws_row = m->rows};
  int mh, mx, mw, my, sy, sw, nm, ns, i, n;

  n = m->clients.length;
  struct vte_host *c;
  vec_foreach(c, m->clients) c->border_width = 1;

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
    struct vte_host *p = vec_nth(m->clients, i);
    struct bounds b = {.x = mx, .y = my, .w = mw, .h = mh};
    vte_host_resize(p, b);
    p = p->next;
    my += mh;
  }

  int stack_height_left = (float)ws.ws_row;
  int stack_items_left = ns;

  for (; i < n; i++) {
    struct vte_host *p = vec_nth(m->clients, i);
    int height = (float)stack_height_left / stack_items_left;
    struct bounds b = {.x = mw, .y = sy, .w = sw, .h = height};
    vte_host_resize(p, b);
    p = p->next;
    sy += height;
    stack_items_left--;
    stack_height_left -= height;
  }
}

static bool handle_keybinds(struct multiplexer *m, uint8_t ch) {
  return false;
}

void multiplexer_feed_input(struct multiplexer *m, uint8_t *buf, int n) {
  struct vte_host *focused = vec_nth(m->clients, m->focus);

  static struct string writebuffer = {0};
  static struct string pastebuffer = {0};
  static enum stdin_state {
    normal,
    esc,
    csi,
    bracketed_paste,
  } s;

  static const struct running_hash paste_start = {.characters = "\x1b[200~"};
  static const struct running_hash paste_end = {.characters = "\x1b[201~"};
  static struct running_hash running_hash = {0};

  string_clear(&writebuffer);

  for (int i = 0; i < n; i++) {
    uint8_t ch = buf[i];
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
    switch (s) {
    case bracketed_paste: string_push_char(&pastebuffer, ch); break;
    case normal: {
      if (ch == 0x1b) {
        s = esc;
      } else if (handle_keybinds(m, ch)) {
        continue;
      } else {
        string_push_char(&writebuffer, ch);
      }
    } break;
    case esc: {
      if (ch == '[') {
        s = csi;
      } else {
        // escape next char
        string_push_char(&writebuffer, 0x1b);
        string_push_char(&writebuffer, ch);
        s = normal;
      }
    } break;
    case csi: {
      if (ch >= 'A' && ch <= 'D' && focused->vte.options.application_mode) {
        string_push_char(&writebuffer, 0x1b);
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
        string_push_char(&writebuffer, 0x1b);
        string_push_char(&writebuffer, '[');
        string_push_char(&writebuffer, ch);
      }
      s = normal;
    } break;
    }
  }

  // TODO: Implement timing mechanism for escapes.
  // For now, flush before return to restore state machine to normal input mode
  if (s == csi) {
    string_push_char(&writebuffer, 0x1b);
    string_push_char(&writebuffer, '[');
    s = normal;
  } else if (s == esc) {
    string_push_char(&writebuffer, 0x1b);
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
  assert(m->clients.element_size == sizeof(struct vte_host));
  logmsg("Spawn %s", process);
  struct vte_host *host = vec_new_element(&m->clients);
  host->process = strdup(process);
  host->vte = vte_default;
  multiplexer_arrange(m);
  vte_host_start(host);
}

void multiplexer_destroy(struct multiplexer *m) {
  vec_destroy(&m->clients);
}

void multiplexer_remove_exited(struct multiplexer *m) {
  int status;
  pid_t pid;

  while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
    if (WIFEXITED(status)) {
      for (size_t i = 0; i < m->clients.length; i++) {
        struct vte_host *h = vec_nth(m->clients, i);
        if (h->pid == pid) {
          // zero the pid to indicate the process exited.
          // otherwise the process will be reaped in vte_host_destroy
          h->pid = 0;
          vte_host_destroy(h);
          vec_remove(&m->clients, i);
          break;
        }
      }
    }
  }
  multiplexer_arrange(m);
}

void multiplexer_resize(struct multiplexer *m, int rows, int columns) {
  m->rows = rows;
  m->columns = columns;
  multiplexer_arrange(m);
}
