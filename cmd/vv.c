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
    p->x = mx;
    p->y = my;
    my += mh;
    pane_resize(p, mw, mh);
    p = p->next;
  }

  for (; i < n; i++) {
    p->x = mw;
    p->y = sy;
    sy += sh;
    pane_resize(p, sw, sh);
    p = p->next;
  }
}

static void pane_focus(struct pane *pane, struct string *str) {
  if (pane && pane->fsm.active_grid) {
    focused = pane;
    char fmt[40];
    // set cursor position within the pane
    struct grid *g = pane->fsm.active_grid;
    struct cursor *c = &g->cursor;
    int lineno = 1 + pane->y + (c->y - g->offset + g->h) % g->h;
    int columnno = 1 + pane->x + c->x;
    int n = snprintf((char *)fmt, sizeof(fmt), "\x1b[%d;%dH", lineno, columnno);
    // write(STDOUT_FILENO, fmt, n);
    string_push(str, fmt, n);
  }
}

static void focusnext(void) {
  if (focused && focused->next) {
    focused = focused->next;
  } else {
    focused = lst;
  }
}

int main(int argc, char **argv) {
  enter_alternate_screen();
  enable_raw_mode();

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
  focused = lst;

  struct pollfd *fds = calloc(1 + pane_count(lst), sizeof(struct pollfd));
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
  bool running = true;
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
            arrange(ws, lst);
            for (struct pane *p = lst; p; p = p->next) {
              grid_invalidate(p->fsm.active_grid);
            }
            break;
          default:
            running = false;
            break;
          }
        }
      } else {
        die("poll %d:", errno);
      }
    }

    if (polled <= 0) continue;

    if (fds[0].revents & POLL_IN) {
      // handle stdin
      int n = read(STDIN_FILENO, readbuffer, sizeof(readbuffer));
      if (n == -1) {
        die("read:");
      }
      if (n == 0) {
        break;
      }
      for (int i = 0; i < n; i++) {
        if (readbuffer[i] == CTRL('J')) {
          focusnext();
          continue;
        }
        if (readbuffer[i] == CTRL('W')) {
          logmsg("Exit by ^W");
          running = false;
          break;
        }
        // TODO: batched writes
        write(focused->pty, readbuffer + i, 1);
      }
      if (n == -1) {
        die("read:");
      }
    }

    for (int i = 1; i < nfds; i++) {
      if (fds[i].revents & POLL_IN) {
        struct pane *p = pane_from_pty(lst, fds[i].fd);
        if (p) {
          uint8_t buf[1 << 16];
          int n = read(p->pty, buf, sizeof(buf));
          if (n == -1) {
            if (errno == EAGAIN) continue;
            die("read:");
          }
          if (n > 0) {
            pane_write(p, buf, n);
          }
        }
      }
    }

    string_clear(&draw_buffer);
    // TODO: Don't write cursor if panes didn't draw
    char hide_cursor[] = "\x1b[?25l";
    char show_cursor[] = "\x1b[?25h";
    string_push(&draw_buffer, hide_cursor, sizeof(hide_cursor));
    size_t initial_bytes = draw_buffer.len;
    for (struct pane *p = lst; p; p = p->next) {
      pane_draw(p, false, &draw_buffer);
    }

    if (!focused) focused = lst;
    if (focused) pane_focus(focused, &draw_buffer);
    if (focused && focused->fsm.opts.cursor_hidden == false)
      string_push(&draw_buffer, show_cursor, sizeof(show_cursor));

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
            focused = p->next;
          }
          pane_remove(&lst, p);
          pane_destroy(p);
          if (!focused) focused = lst;
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
  exit_raw_mode();
  leave_alternate_screen();
  printf("[exited]\n");
  free(fds);
}
