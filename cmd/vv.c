#include <assert.h>
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
  write(sigpipe.write, &sig, sizeof(sig));
}

static void set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL);
  if (flags == -1 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    die("fcntl:");
  }
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

  if (pipe(sigpipe.pipes) < 0) {
    die("pipe:");
  }
  set_nonblocking(sigpipe.read);
  set_nonblocking(sigpipe.write);
}

static int nmaster = 1;
static float factor = 0.5;

static struct pane *focused = NULL;
static struct pane *lst = NULL;

static void arrange(struct winsize ws, struct pane *p) {
  if (!p) return;
  int mh, sh, mx, mw, my, sy, sw, nm, ns, i, n;
  for (struct pane *c = p; c; c = c->next) c->border_width = 1;

  n = pane_count(p);

  i = my = sy = sw = mx = 0;
  nm = n > nmaster ? nmaster : n;
  ns = n > nmaster ? n - nmaster : 0;

  mh = (int)((float)ws.ws_row / (float)nm);
  sh = (int)((float)ws.ws_row / (float)ns);

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
    char fmt[40];
    // set cursor position within the pane
    struct grid *g = pane->fsm.active_grid;
    struct raw_cursor *c = &g->cursor;
    int lineno = 1 + pane->rect.client.y + (c->y - g->offset + g->h) % g->h;
    int columnno = 1 + pane->rect.client.x + c->x;
    int n = snprintf((char *)fmt, sizeof(fmt), "\x1b[%d;%dH", lineno, columnno);
    // write(STDOUT_FILENO, fmt, n);
    string_push_slice(drawbuffer, fmt, n);
  }
}

static void pane_notify_focus(struct pane *p, bool focused) {
  if (p) {
    p->border_dirty = true;
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
  if (focused == lst) {
    for (c = lst; c && c->next; c = c->next);
    focus_pane(c);
  } else {
    for (c = lst; c && c->next != focused; c = c->next);
    if (c && c->next == focused) focus_pane(c);
  }
}

static void focusnext(void) {
  if (focused && focused->next) {
    focus_pane(focused->next);
  } else {
    focus_pane(lst);
  }
}

static bool running = true;
static void handle_stdin(const char *const buf, int n, struct string *draw_buffer) {
  enum stdin_state {
    normal,
    esc,
    csi,
  };
  static enum stdin_state s = normal;

  static struct string writebuffer = {0};
  string_clear(&writebuffer);

  for (int i = 0; i < n; i++) {
    char ch = buf[i];

    switch (s) {
    case normal: {
      switch (ch) {
      case 0x1b: {
        s = esc;
      } break;
      case CTRL('N'): {
        if (pane_count(lst) < 6) {
          struct pane *new = calloc(1, sizeof(*new));
          new->process = strdup("bash");
          new->next = lst;
          lst = new;
          arrange(ws, lst);
          pane_start(new);
          set_nonblocking(new->pty);
          focus_pane(new);
          string_push(draw_buffer, "\x1b[2J");
        } else {
          logmsg("Too many panes. Spawn request ignored.");
        }
      } break;
      case CTRL('G'): {
        if (lst != focused) {
          pane_remove(&lst, focused);
          focused->next = lst;
          lst = focused;
          arrange(ws, lst);
        }
      } break;
      case CTRL('A'): {
        nmaster = MIN(pane_count(lst), nmaster + 1);
        arrange(ws, lst);
      } break;
      case CTRL('X'): {
        nmaster = MAX(0, nmaster - 1);
        arrange(ws, lst);
      } break;
      case CTRL('W'): {
        logmsg("Exit by ^W");
        running = false;
      } break;
      case CTRL('K'): {
        focusprev();
      } break;
      case CTRL('J'): {
        focusnext();
      } break;
      default: {
        string_push_char(&writebuffer, ch);
      } break;
      }
      break;
    } break;
    case esc: {
      switch (ch) {
      case '[': {
        s = csi;
      } break;
      default: {
        s = normal;
        string_push_char(&writebuffer, 0x1b);
        string_push_char(&writebuffer, ch);
      } break;
      }
    } break;
    case csi: {
      // TODO: Bracketed paste
      if (ch >= 'A' && ch <= 'D' && focused->fsm.features.application_mode) {
        string_push_char(&writebuffer, 0x1b);
        string_push_char(&writebuffer, 'O');
        string_push_char(&writebuffer, ch);
      } else if (ch == 'O' || ch == 'I') {
        // Focus event. Forward it if the focused pane has the feature enabled
        bool did_focus = ch == 'I';
        if (focused->fsm.features.focus_reporting) {
          char *s = did_focus ? focus_in : focus_out;
          string_push_slice(&writebuffer, s, strlen(s));
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

  write(focused->pty, writebuffer.content, writebuffer.len);
}

int main(int argc, char **argv) {
  enable_raw_mode_etc();

  install_signal_handlers();

  {
    struct pane *prev = NULL;
    for (int i = 1; i < argc; i++) {
      struct pane *p = calloc(1, sizeof(*p));
      p->process = strdup(argv[i]);
      logmsg("Create %s", p->process);
      if (!lst) {
        // first element -- asign head
        lst = p;
        prev = lst;
      } else {
        // Otherwise append to previous element
        prev->next = p;
      }
      prev = p;
    }
  }

  arrange(ws, lst);
  focus_pane(lst);

  struct pollfd *fds = calloc(100, sizeof(struct pollfd));
  fds[0].fd = fileno(stdin);
  fds[0].events = POLL_IN;
  int nfds = 1;

  {
    int i = 0;
    for (struct pane *p = lst; p; p = p->next) {
      pane_start(p);
      set_nonblocking(p->pty);
      fds[i + 1].fd = p->pty;
      fds[i + 1].events = POLL_IN;
      i++;
    }
    nfds = 1 + i;
  }

  static struct string draw_buffer = {0};

  char readbuffer[4096];
  for (; running && pane_count(lst);) {
    int polled = poll(fds, nfds, -1);
    if (polled == -1) {
      if (errno == EAGAIN) continue;
      if (errno == EINTR) {
        int signal = 0;
        if (read(sigpipe.read, &signal, sizeof(signal)) > 0) {
          switch (signal) {
          case SIGWINCH:
            if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
              die("ioctl TIOCGWINSZ:");
            }
            for (struct pane *p = lst; p; p = p->next) {
              grid_invalidate(p->fsm.active_grid);
              p->border_dirty = true;
            }
            arrange(ws, lst);
            char clear[] = "\x1b[2J";
            string_push_slice(&draw_buffer, clear, sizeof(clear) - 1);
            break;
          default: running = false; break;
          }
        }
      } else {
        die("poll %d:", errno);
      }
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
        struct pane *p = pane_from_pty(lst, fds[i].fd);
        if (p) {
          uint8_t buf[1 << 16];
          int n = read(p->pty, buf, sizeof(buf));
          if (n == -1) {
            if (errno == EAGAIN || errno == EINTR) continue;
            die("read %s:", p->process);
          }
          if (n > 0) {
            pane_write(p, buf, n);
          }
        }
      }
    }

    // TODO: Don't write cursor if panes didn't draw
    char hide_cursor[] = "\x1b[?25l";
    char show_cursor[] = "\x1b[?25h";
    string_push_slice(&draw_buffer, hide_cursor, sizeof(hide_cursor));
    size_t initial_bytes = draw_buffer.len;
    for (struct pane *p = lst; p; p = p->next) {
      pane_draw(p, false, &draw_buffer);
    }

    string_push(&draw_buffer, "\x1b[0m");
    for (struct pane *p = lst; p; p = p->next) {
      /* TODO: Move styling logic into draw function
       * Then apply style changes using the appropriate apply_style function
       */
      if (p == focused) {
        string_push(&draw_buffer, "\x1b[31m");
        string_push(&draw_buffer, "\x1b[1m");
      } else {
        string_push(&draw_buffer, "\x1b[0m");
        string_push(&draw_buffer, "\x1b[34m");
      }
      pane_draw_border(p, &draw_buffer);
    }
    string_push(&draw_buffer, "\x1b[0m");

    if (!focused) focus_pane(lst);
    if (focused) move_cursor_to_pane(focused, &draw_buffer);
    if (focused && focused->fsm.features.cursor_hidden == false)
      string_push_slice(&draw_buffer, show_cursor, sizeof(show_cursor));

    if (draw_buffer.len != initial_bytes) {
      string_flush(&draw_buffer, STDOUT_FILENO, NULL);
    }

    {
      for (struct pane *p = lst; p;) {
        int status;
        pid_t result = waitpid(p->pid, &status, WNOHANG);
        if (result == p->pid && WIFEXITED(status)) {
          struct pane *next = p->next;
          p->pid = 0;
          if (p == focused) {
            focus_pane(p->next);
          }
          pane_remove(&lst, p);
          pane_destroy(p);
          free(p);
          if (!focused) focus_pane(lst);
          p = next;
        } else {
          p = p->next;
        }
      }
    }

    { // update fd set
      int i = 0;
      for (struct pane *p = lst; p; p = p->next) {
        fds[i + 1].fd = p->pty;
        fds[i + 1].events = POLL_IN;
        i++;
      }
      nfds = 1 + i;
    }
    arrange(ws, lst);
  }

  string_destroy(&draw_buffer);

  disable_raw_mode_etc();
  printf("[exited]\n");
  free(fds);
}
