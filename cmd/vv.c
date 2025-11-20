#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "collections.h"
#include "vte.h"
#include "vte_host.h"
#include "virtual_terminal_sequences.h"
#include "utils.h"
#include <sys/wait.h>

#ifndef CTRL
#define CTRL(x) ((x) & 037)
#endif

static struct {
  union {
    int pipes[2];
    struct {
      int read;
      int write;
    };
  };
} sigpipe;

static void signal_handler(int sig, siginfo_t *siginfo, void *context) {
  (void)siginfo, (void)context;
  size_t written = write(sigpipe.write, &sig, sizeof(sig));
  if (written < sizeof(sig)) die("signal write:");
}

static void install_signal_handlers(void) {
  struct sigaction sa = {0};
  sa.sa_sigaction = &signal_handler;
  sa.sa_flags = SA_SIGINFO;
  if (sigaction(SIGWINCH, &sa, NULL) == -1) {
    die("sigaction:");
  }
  if (sigaction(SIGINT, &sa, NULL) == -1) {
    die("sigaction:");
  }
  if (sigaction(SIGCHLD, &sa, NULL) == -1) {
    die("sigaction:");
  }

  if (pipe(sigpipe.pipes) < 0) {
    die("pipe:");
  }
  set_nonblocking(sigpipe.read);
}

static int nmaster = 1;
static float factor = 0.5;

static void arrange(struct winsize ws, struct vte_host *p) {
  if (!p) return;
  int mh, mx, mw, my, sy, sw, nm, ns, i, n;

  n = vte_host_count(p);
  // if (n == 1)
  //   p->border_width = 0;
  // else
  for (struct vte_host *c = p; c; c = c->next) c->border_width = 1;

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
    struct bounds b = {.x = mx, .y = my, .w = mw, .h = mh};
    vte_host_resize(p, b);
    p = p->next;
    my += mh;
  }

  int stack_height_left = (float)ws.ws_row;
  int stack_items_left = ns;

  for (; i < n; i++) {
    int height = (float)stack_height_left / stack_items_left;
    struct bounds b = {.x = mw, .y = sy, .w = sw, .h = height};
    vte_host_resize(p, b);
    p = p->next;
    sy += height;
    stack_items_left--;
    stack_height_left -= height;
  }
}

static void move_cursor_to_vte_host(struct vte_host *vte_host, struct string *drawbuffer) {
  if (vte_host && vte_host->vte.active_grid) {
    focused = vte_host;
    // set cursor position within the vte_host
    struct grid *g = vte_host->vte.active_grid;
    struct cursor *c = &g->cursor;
    int lineno = 1 + vte_host->rect.client.y + c->row;
    int columnno = 1 + vte_host->rect.client.x + c->col;
    string_push_csi(drawbuffer, INT_SLICE(lineno, columnno), "H");
  }
}

static void focus_vte_host(struct vte_host *p) {
  if (p != focused) {
    vte_host_notify_focus(focused, false);
    vte_host_notify_focus(p, true);
  }
  focused = p;
}

static void focusprev(void) {
  struct vte_host *c;
  if (focused == clients) {
    for (c = clients; c && c->next; c = c->next);
    focus_vte_host(c);
  } else {
    for (c = clients; c && c->next != focused; c = c->next);
    if (c && c->next == focused) focus_vte_host(c);
  }
}

static void focusnext(void) {
  if (focused && focused->next) {
    focus_vte_host(focused->next);
  } else {
    focus_vte_host(clients);
  }
}

static void detachstack(struct vte_host *p) {
  vte_host_remove(&clients, p);
}
static void attachstack(struct vte_host *p) {
  p->next = clients;
  clients = p;
}

static void zoom(void) {
  struct vte_host *current_main = clients;
  struct vte_host *new_main = focused;
  if (current_main == new_main) new_main = current_main->next;
  if (!new_main) return;
  detachstack(new_main);
  attachstack(new_main);
  focus_vte_host(new_main);
  arrange(ws_current, clients);
}

static bool running = true;

static void new_client() {
    if (vte_host_count(clients) < 6) {
      struct vte_host *new = calloc(1, sizeof(*new));
      // TODO: Start user's preferred shell
      new->process = strdup("zsh");
      new->next = clients;
      new->vte = vte_default;
      clients = new;
      arrange(ws_current, clients);
      vte_host_start(new);
      focus_vte_host(new);
    } else {
      logmsg("Too many vte_hosts. Spawn request ignored.");
    }
}

bool handle_keybinds(uint8_t ch) {
  switch (ch) {
  case CTRL('N'):
      new_client();
    return true;
  case CTRL('G'): zoom(); return true;
  case CTRL('A'):
    nmaster = MIN(vte_host_count(clients), nmaster + 1);
    arrange(ws_current, clients);
    return true;
  case CTRL('X'):
    nmaster = MAX(0, nmaster - 1);
    arrange(ws_current, clients);
    return true;
  case CTRL('K'): focusprev(); return true;
  case CTRL('J'): focusnext(); return true;
  }
  return false;
}

static void handle_stdin(const char *const buf, int n, struct string *draw_buffer) {
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
      } else if (handle_keybinds(ch)) {
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
    write(focused->pty, writebuffer.content, writebuffer.len);
  }
}

static void vte_host_remove_and_destroy(struct vte_host *p) {
  if (p) {
    p->pid = 0;
    if (p == focused) focusnext();
    // Special case when `p` was the last vte_host
    if (p == focused) focused = nullptr;
    vte_host_remove(&clients, p);
    vte_host_destroy(p);
    free(p);
  }
}

void remove_exited_processes(void) {
  int status;
  pid_t pid;

  while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
    if (WIFEXITED(status)) {
      struct vte_host *p = vte_host_from_pid(clients, pid);
      vte_host_remove_and_destroy(p);
    }
  }
}

void handle_sigwinch(struct string *draw_buffer) {
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws_current) == -1) {
    die("ioctl TIOCGWINSZ:");
  }
  for (struct vte_host *p = clients; p; p = p->next) {
    grid_invalidate(p->vte.active_grid);
    p->border_dirty = true;
  }
  arrange(ws_current, clients);
  string_push_slice(draw_buffer, vt_clear);
}

static void vte_host_draw_borders(struct string *draw_buffer) {
  for (struct vte_host *p = clients; p; p = p->next) {
    if (p == focused) continue;
    vte_host_draw_border(p, draw_buffer);
  }
  if (focused) vte_host_draw_border(focused, draw_buffer);
}

static void render_frame(struct string *draw_buffer) {
  static enum cursor_style current_cursor_style = 0;

  string_push_slice(draw_buffer, vt_hide_cursor);
  for (struct vte_host *p = clients; p; p = p->next) {
    vte_host_update_cwd(p);
    vte_host_draw(p, false, draw_buffer);
  }
  vte_host_draw_borders(draw_buffer);

  if (!focused) focus_vte_host(clients);
  if (focused) move_cursor_to_vte_host(focused, draw_buffer);
  if (focused && focused->vte.options.cursor.style != current_cursor_style) {
    current_cursor_style = focused->vte.options.cursor.style;
    string_push_csi(draw_buffer, INT_SLICE(current_cursor_style), " q");
  }
  if (focused && focused->vte.options.cursor.visible) string_push_slice(draw_buffer, vt_show_cursor);

  string_flush(draw_buffer, STDOUT_FILENO, NULL);
}

int main(int argc, char **argv) {
  terminal_setup();
  install_signal_handlers();

  {
    struct vte_host *prev = NULL;
    for (int i = 1; i < argc; i++) {
      struct vte_host *p = calloc(1, sizeof(*p));
      memcpy(&p->vte, &vte_default, sizeof(vte_default));
      p->process = strdup(argv[i]);
      if (!prev) {
        // first element -- asign head
        clients = p;
      } else {
        // Otherwise append to previous element
        prev->next = p;
      }
      prev = p;
    }

    if (argc < 2) {
      clients = calloc(1, sizeof(*clients));
      clients->process = strdup("zsh");
      memcpy(&clients->vte, &vte_default, sizeof(vte_default));
    }
  }

  arrange(ws_current, clients);
  focus_vte_host(clients);

  struct pollfd *fds = calloc(100, sizeof(struct pollfd));
  fds[0].fd = fileno(stdin);
  fds[0].events = POLL_IN;
  int nfds = 1;

  {
    int i = 0;
    for (struct vte_host *p = clients; p; p = p->next) {
      vte_host_start(p);
      fds[i + 1].fd = p->pty;
      fds[i + 1].events = POLL_IN;
      i++;
    }
    nfds = 1 + i;
  }

  struct string draw_buffer = {0};
  char readbuffer[4096];

  for (; running && vte_host_count(clients);) {
    int polled = poll(fds, nfds, -1);
    if (polled == -1) {
      if (errno != EAGAIN && errno != EINTR) {
        die("poll %d:", errno);
      }
      if (errno == EAGAIN) continue;
    }
    {
      int signal = 0;
      bool did_sigwinch = false;
      bool did_sigchild = false;
      while (read(sigpipe.read, &signal, sizeof(signal)) > 0) {
        switch (signal) {
        case SIGWINCH: did_sigwinch = true; break;
        case SIGCHLD: did_sigchild = true; break;
        default:
          running = false;
          logmsg("Unhandled signal: %d", signal);
          break;
        }
      }

      if (did_sigchild) remove_exited_processes();

      // This happens if SIGCHLD was raised and all clients exited
      if (clients == nullptr) {
        running = false;
        continue;
      }

      if (did_sigwinch) handle_sigwinch(&draw_buffer);
    }

    if (fds[0].revents & POLL_IN) {
      polled--;
      int n = read(STDIN_FILENO, readbuffer, sizeof(readbuffer));
      if (n == -1 && errno != EINTR) die("read stdin:"); // EINTR: a signal was raised during the read
      if (n == 0) break;                                 // EOF
      if (n > 0) handle_stdin(readbuffer, n, &draw_buffer);
    }

    for (int i = 1; polled > 0 && i < nfds; i++) {
      if (fds[i].revents & POLL_IN) {
        polled--;
        struct vte_host *p = vte_host_from_pty(clients, fds[i].fd);
        assert(p);
        /* These parameters greatly affect throughput when an application is writing in a busy loop.
         * Observations from brief testing on Mac in Alacritty:
         * Raw ascii output was generated with dd if=/dev/urandom | base64
         * Increasing BUFSIZE above 4k only hurts performance
         * Responsiveness of other vte_hosts depends on the MAX_IT variable. From my testing,
         * vv is completely responsive even when 4 vte_hosts are generating garbage full throttle.
         * In a similar scenario in tmux, I observed a lot of flickering, but that is not the case in vv.
         * Raising MAX_IT greatly increases throughput, but past 512kb the gains are marginal (Unbounded is ~5%
         * faster), so responsiveness is a more important metric.
         *
         * On MacOS, I observed the maximum read from the PTY to be 1024, but it could be different on other
         * platforms, and the gains of decreasing the buffer size are also very marginal (~3%)
         *
         * TODO: Benchmark performance with ansi escapes instead of just ascii
         */
        constexpr int BUFSIZE = 4096;      // 4kb
        constexpr int MAX_BYTES = 1 << 19; // 512kb
        constexpr int MAX_IT = MAX_BYTES / BUFSIZE;
        uint8_t buf[BUFSIZE];
        int n = 0, iterations = 0;
        while (iterations < MAX_IT && (n = read(p->pty, buf, BUFSIZE)) > 0) {
          vte_host_process_output(p, buf, n);
          iterations++;
        }
        if (n == -1 && errno != EAGAIN && errno != EINTR) {
          die("read %s:", p->process);
        } else if (n == 0) {
          vte_host_remove_and_destroy(p); // pipe closed -- destroy vte_host
        }
      }
    }

    render_frame(&draw_buffer);

    { // update fd set
      int i = 0;
      for (struct vte_host *p = clients; p; p = p->next) {
        fds[i + 1].fd = p->pty;
        fds[i + 1].events = POLL_IN;
        // if we have pending writes, monitor pollout
        if (p->vte.pending_output.len > 0) fds[i + 1].events |= POLL_OUT;
        i++;
      }
      nfds = 1 + i;
    }
    arrange(ws_current, clients);
  }

  string_destroy(&draw_buffer);

  terminal_reset();
  printf("[exited]\n");
  free(fds);
}
