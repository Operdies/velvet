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
#include "emulator.h"
#include "pane.h"
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

static const char *const focus_out = "\x1b[O";
static const char *const focus_in = "\x1b[I";

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

static void arrange(struct winsize ws, struct pane *p) {
  if (!p) return;
  int mh, mx, mw, my, sy, sw, nm, ns, i, n;

  n = pane_count(p);
  // if (n == 1)
  //   p->border_width = 0;
  // else
  for (struct pane *c = p; c; c = c->next) c->border_width = 1;

  i = my = sy = sw = mx = 0;
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
    pane_resize(p, b);
    p = p->next;
    my += mh;
  }

  int stack_height_left = (float)ws.ws_row;
  int stack_items_left = ns;

  for (; i < n; i++) {
    int height = (float)stack_height_left / stack_items_left;
    struct bounds b = {.x = mw, .y = sy, .w = sw, .h = height};
    pane_resize(p, b);
    p = p->next;
    sy += height;
    stack_items_left--;
    stack_height_left -= height;
  }
}

static void move_cursor_to_pane(struct pane *pane, struct string *drawbuffer) {
  if (pane && pane->fsm.active_grid) {
    focused = pane;
    uint8_t fmt[40];
    // set cursor position within the pane
    struct grid *g = pane->fsm.active_grid;
    struct raw_cursor *c = &g->cursor;
    int lineno = 1 + pane->rect.client.y + (c->row - g->offset + g->h) % g->h;
    int columnno = 1 + pane->rect.client.x + c->col;
    int n = snprintf((char *)fmt, sizeof(fmt), "\x1b[%d;%dH", lineno, columnno);
    // write(STDOUT_FILENO, fmt, n);
    string_push_slice(drawbuffer, fmt, n);
  }
}

static void pane_notify_focus(struct pane *p, bool focused) {
  if (p) {
    p->border_dirty = true;
    p->has_focus = focused;
    if (p->pty && p->fsm.features.focus_reporting) {
      if (focused) {
        write(p->pty, focus_in, strlen(focus_in));
      } else {
        write(p->pty, focus_out, strlen(focus_out));
      }
    }
  }
}

static void focus_pane(struct pane *p) {
  if (p != focused) {
    pane_notify_focus(focused, false);
    pane_notify_focus(p, true);
  }
  focused = p;
}

static void focusprev(void) {
  struct pane *c;
  if (focused == clients) {
    for (c = clients; c && c->next; c = c->next);
    focus_pane(c);
  } else {
    for (c = clients; c && c->next != focused; c = c->next);
    if (c && c->next == focused) focus_pane(c);
  }
}

static void focusnext(void) {
  if (focused && focused->next) {
    focus_pane(focused->next);
  } else {
    focus_pane(clients);
  }
}

static void detachstack(struct pane *p) {
  pane_remove(&clients, p);
}
static void attachstack(struct pane *p) {
  p->next = clients;
  clients = p;
}

static void zoom(void) {
  struct pane *current_main = clients;
  struct pane *new_main = focused;
  if (current_main == new_main) new_main = current_main->next;
  if (!new_main) return;
  detachstack(new_main);
  attachstack(new_main);
  focus_pane(new_main);
  arrange(ws_current, clients);
}

static bool running = true;

bool handle_keybinds(uint8_t ch, struct string *draw_buffer) {
  switch (ch) {
  case CTRL('N'):
    if (pane_count(clients) < 6) {
      struct pane *new = calloc(1, sizeof(*new));
      // TODO: Start user's preferred shell
      new->process = strdup("zsh");
      new->next = clients;
      memcpy(&new->fsm, &fsm_default, sizeof(fsm_default));
      clients = new;
      arrange(ws_current, clients);
      pane_start(new);
      focus_pane(new);
      string_push(draw_buffer, u8"\x1b[2J");
    } else {
      logmsg("Too many panes. Spawn request ignored.");
    }
    return true;
  case CTRL('G'): zoom(); return true;
  case CTRL('A'):
    nmaster = MIN(pane_count(clients), nmaster + 1);
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

static inline uint64_t dumbest_hash(uint8_t *str) {
  uint64_t hash = 0;
  for (; *str; str++) hash = (hash << 8) | *str;
  return hash;
}

// Who needs to paste more than 1mb lol
#define PASTEBUFFER_MAX (1 << 20)
static void handle_stdin(const char *const buf, int n, struct string *draw_buffer) {
  static struct string writebuffer = {0};
  static struct string pastebuffer = {0};
  static enum stdin_state {
    normal,
    esc,
    csi,
    bracketed_paste,
  } s;

  uint64_t paste_start = dumbest_hash((uint8_t *)"\x1b[200~");
  uint64_t paste_end = dumbest_hash((uint8_t *)"\x1b[201~");
  uint64_t pastebuf = 0;

  string_clear(&writebuffer);

  for (int i = 0; i < n; i++) {
    uint8_t ch = buf[i];
    pastebuf = ((pastebuf << 8) | ch) & 0x00ffffffffffff;
    if (s == normal && pastebuf == paste_start) {
      writebuffer.len -= 5;
      s = bracketed_paste;
      continue;
    } else if (s == bracketed_paste && pastebuf == paste_end) {
      pastebuffer.len -= 5;
      s = normal;
      if (focused && focused->fsm.features.bracketed_paste) {
        string_push(&writebuffer, u8"\x1b[200~");
        string_push_slice(&writebuffer, pastebuffer.content, pastebuffer.len);
        string_push(&writebuffer, u8"\x1b[201~");
      } else {
        string_push_slice(&writebuffer, pastebuffer.content, pastebuffer.len);
      }
      string_clear(&pastebuffer);
      continue;
    }
    switch (s) {
    case bracketed_paste: string_push_char(&pastebuffer, ch); break;
    case normal: {
      if (ch == 0x1b) {
        s = esc;
      } else if (handle_keybinds(ch, draw_buffer)) {
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
      if (ch >= 'A' && ch <= 'D' && focused->fsm.features.application_mode) {
        string_push_char(&writebuffer, 0x1b);
        string_push_char(&writebuffer, 'O');
        string_push_char(&writebuffer, ch);
      } else if (ch == 'O' || ch == 'I') {
        // Focus event. Forward it if the focused pane has the feature enabled
        bool did_focus = ch == 'I';
        if (focused->fsm.features.focus_reporting) {
          const char *s = did_focus ? focus_in : focus_out;
          string_push_slice(&writebuffer, (uint8_t *)s, strlen(s));
        }
        // If the pane does not have the feature enabled, ignore it.
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
  // For now, flush flush before return to restore state machine to normal input mode
  if (s == csi) {
    string_push_char(&writebuffer, 0x1b);
    string_push_char(&writebuffer, '[');
    s = normal;
  } else if (s == esc) {
    string_push_char(&writebuffer, 0x1b);
    s = normal;
  }

  write(focused->pty, writebuffer.content, writebuffer.len);
}

static void pane_remove_and_destroy(struct pane *p) {
  if (p) {
    p->pid = 0;
    if (p == focused) focusnext();
    // Special case when `p` was the last pane
    if (p == focused) focused = nullptr;
    pane_remove(&clients, p);
    pane_destroy(p);
    free(p);
  }
}

void remove_exited_processes(void) {
  int status;
  pid_t pid;

  while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
    if (WIFEXITED(status)) {
      struct pane *p = pane_from_pid(clients, pid);
      pane_remove_and_destroy(p);
    }
  }
}

void handle_sigwinch(struct string *draw_buffer) {
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws_current) == -1) {
    die("ioctl TIOCGWINSZ:");
  }
  for (struct pane *p = clients; p; p = p->next) {
    grid_invalidate(p->fsm.active_grid);
    p->border_dirty = true;
  }
  arrange(ws_current, clients);
  uint8_t clear[] = "\x1b[2J";
  string_push_slice(draw_buffer, clear, sizeof(clear) - 1);
}

static void string_write_arg_list(struct string *str, uint8_t *escape, int n, int *args, char terminator) {
  string_push(str, escape);
  for (int i = 0; i < n; i++) {
    string_push_int(str, args[i]);
    string_push_char(str, ';');
  }
  if (n) str->len--;
  string_push_char(str, terminator);
}

static void handle_queries(struct pane *p) {
  static struct string response = {0};
  struct emulator_query *req;
  vec_foreach(req, p->fsm.pending_requests) {
    switch (req->type) {
    case REQUEST_CURSOR_POSITION: {
      int args[] = {p->fsm.active_grid->cursor.row, p->fsm.active_grid->cursor.col};
      string_write_arg_list(&response, u8"\x1b[", 2, args, 'R');
    } break;
    case REQUEST_DEFAULT_FG: string_push(&response, u8"\x1b]10;rgb:ffffff\a"); break;
    case REQUEST_DEFAULT_BG:
      string_push(&response, u8"\x1b]11;rgb:1e1e/1e1e/2e2e");
      string_push(&response, req->st);
      break;
    case REQUEST_DEFAULT_CURSOR_COLOR: string_push(&response, u8"\x1b]10;rgb:ffffff\a"); break;
    case REQUEST_PRIMARY_DEVICE_ATTRIBUTES:
    case REQUEST_SECONDARY_DEVICE_ATTRIBUTES:
    case REQUEST_TERTIARY_DEVICE_ATTRIBUTES:
    case REQUEST_STATUS_OK:
    case REQUEST_WINDOW_TITLE:
    case REQUEST_WINDOW_ICON:
    case REQUEST_BRACKETED_PASTE_STATUS:
    case REQUEST_FOCUS_REPORTING_STATUS:
    case REQUEST_EXTENDED_FEATURES:
    case REQUEST_TRUECOLOR_STATUS:
    default: logmsg("Handle query type: %s", emulator_query_type_names[req->type]); break;
    }
  }

  vec_clear(&p->fsm.pending_requests);
  if (response.len) {
    write(p->pty, response.content, response.len);
  }
  string_clear(&response);
}

static void pane_draw_borders(struct string *draw_buffer) {
  for (struct pane *p = clients; p; p = p->next) {
    if (p == focused) continue;
    pane_draw_border(p, draw_buffer);
  }
  if (focused) pane_draw_border(focused, draw_buffer);
}

static void render_frame(struct string *draw_buffer) {
  static enum cursor_style current_cursor_style = 0;
  static const uint8_t *hide_cursor = u8"\x1b[?25l";
  static const uint8_t *show_cursor = u8"\x1b[?25h";

  string_push(draw_buffer, hide_cursor);
  for (struct pane *p = clients; p; p = p->next) {
    pane_update_cwd(p);
    pane_draw(p, false, draw_buffer);
  }
  pane_draw_borders(draw_buffer);

  if (!focused) focus_pane(clients);
  if (focused) move_cursor_to_pane(focused, draw_buffer);
  if (focused && focused->fsm.features.cursor.hidden == false) string_push(draw_buffer, show_cursor);
  if (focused && focused->fsm.features.cursor.style != current_cursor_style) {
    current_cursor_style = focused->fsm.features.cursor.style;
    string_push(draw_buffer, u8"\x1b[");
    string_push_int(draw_buffer, current_cursor_style);
    string_push(draw_buffer, u8" q");
  }

  string_flush(draw_buffer, STDOUT_FILENO, NULL);
}

int main(int argc, char **argv) {
  enable_raw_mode_etc();

  install_signal_handlers();

  {
    struct pane *prev = NULL;
    for (int i = 1; i < argc; i++) {
      struct pane *p = calloc(1, sizeof(*p));
      memcpy(&p->fsm, &fsm_default, sizeof(fsm_default));
      p->process = strdup(argv[i]);
      if (!clients) {
        // first element -- asign head
        clients = p;
        prev = clients;
      } else {
        // Otherwise append to previous element
        prev->next = p;
      }
      prev = p;
    }

    if (argc < 2) {
      clients = calloc(1, sizeof(*clients));
      clients->process = strdup("zsh");
      memcpy(&clients->fsm, &fsm_default, sizeof(fsm_default));
    }
  }

  arrange(ws_current, clients);
  focus_pane(clients);

  struct pollfd *fds = calloc(100, sizeof(struct pollfd));
  fds[0].fd = fileno(stdin);
  fds[0].events = POLL_IN;
  int nfds = 1;

  {
    int i = 0;
    for (struct pane *p = clients; p; p = p->next) {
      pane_start(p);
      fds[i + 1].fd = p->pty;
      fds[i + 1].events = POLL_IN;
      i++;
    }
    nfds = 1 + i;
  }

  static struct string draw_buffer = {0};
  render_frame(&draw_buffer);

  char readbuffer[4096];
  for (; running && pane_count(clients);) {
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
      // handle stdin
      int n = read(STDIN_FILENO, readbuffer, sizeof(readbuffer));
      if (n == -1) {
        if (errno == EINTR) {
          // This can happen if the process was signaled during the read
          continue;
        }
        die("read stdin:");
      }
      if (n == 0) {
        break;
      }
      handle_stdin(readbuffer, n, &draw_buffer);
    }

    for (int i = 1; polled > 0 && i < nfds; i++) {
      if (fds[i].revents & POLL_IN) {
        polled--;
        struct pane *p = pane_from_pty(clients, fds[i].fd);
        if (p) {
/* These parameters greatly affect throughput when an application is writing in a busy loop.
 * Observations from brief testing on Mac in Alacritty:
 * Raw ascii output was generated with dd if=/dev/urandom | base64
 * Increasing BUFSIZE above 4k only hurts performance
 * Responsiveness of other panes depends on the MAX_IT variable. From my testing,
 * vv is completely responsive even when 4 panes are generating garbage full throttle.
 * In a similar scenario in tmux, I observed a lot of flickering, but that is not the case in vv.
 * Raising MAX_IT greatly increases throughput, but past 512kb the gains are marginal (Unbounded is ~5% faster), so
 * responsiveness is a more important metric.
 *
 * On MacOS, I observed the maximum read from the PTY to be 1024, but it could be different on other platforms, and the
 * gains of decreasing the buffer size are also very marginal (~3%)
 *
 * TODO: Benchmark performance with ansi escapes instead of just ascii
 */
#define BUFSIZE (4096)      // 4kb
#define MAX_BYTES (1 << 19) // 512kb
#define MAX_IT (MAX_BYTES / BUFSIZE)
          uint8_t buf[BUFSIZE];
          int n = 0, iterations = 0;
          while (iterations < MAX_IT && (n = read(p->pty, buf, BUFSIZE)) > 0) {
            pane_write(p, buf, n);
            iterations++;
          }
          handle_queries(p);
          if (n == -1) {
            if (errno == EAGAIN || errno == EINTR) continue;
            die("read %s:", p->process);
          } else if (n == 0) {
            // pipe closed -- destroy pane
            pane_remove_and_destroy(p);
          }
        }
      }
    }

    render_frame(&draw_buffer);

    { // update fd set
      int i = 0;
      for (struct pane *p = clients; p; p = p->next) {
        fds[i + 1].fd = p->pty;
        fds[i + 1].events = POLL_IN;
        i++;
      }
      nfds = 1 + i;
    }
    arrange(ws_current, clients);
  }

  string_destroy(&draw_buffer);

  disable_raw_mode_etc();
  printf("[exited]\n");
  free(fds);
}
