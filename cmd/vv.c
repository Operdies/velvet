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
#include <sys/signal.h>
#include <sys/ttycom.h>
#include <termios.h>
#include <unistd.h>

#include "emulator.h"
#include "pane.h"
#include "utils.h"

#define MAX_LINES 100
#define MAX_LINE_LENGTH 100

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
  if (sigaction(SIGWINCH, &sa, NULL) < 0) {
    die("sigaction:");
  }
  if (sigaction(SIGINT, &sa, NULL) < 0) {
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

static void pane_focus(struct pane *pane) {
  if (pane && pane->fsm.active_grid) {
    focused = pane;
    char fmt[40];
    // set cursor position within the pane
    struct grid *g = pane->fsm.active_grid;
    struct cursor *c = &g->cursor;
    int lineno = 1 + pane->y + (c->y - g->offset + g->h) % g->h;
    int columnno = 1 + pane->x + c->x;
    int n = snprintf((char *)fmt, sizeof(fmt), "\x1b[%d;%dH", lineno, columnno);
    write(STDOUT_FILENO, fmt, n);
  }
}

static void focusnext(void) {
  if (focused && focused->next) {
    pane_focus(focused->next);
  } else {
    pane_focus(lst);
  }
}

int main(int argc, char **argv) {
  enter_alternate_screen();
  enable_raw_mode();

  install_signal_handlers();

  {
    struct pane *prev = lst;
    for (int i = 1; i < argc; i++) {
      struct pane *p = calloc(1, sizeof(*p));
      p->process = strdup(argv[i]);
      logmsg("Create %s", p->process);
      if (lst) {
        prev->next = p;
      } else {
        lst = p;
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
      fds[i + 1].fd = p->pty;
      fds[i + 1].events = POLL_OUT;
      i++;
    }
    nfds = 1 + i;
  }

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
      if (fds[i].revents & POLL_OUT) {
        struct pane *p = pane_from_pty(lst, fds[i].fd);
        if (p) {
          bool exit = false;
          pane_read(p, &exit);
          if (exit) {
            if (p == focused) focused = p->next;
            pane_remove(&lst, p);
            pane_destroy(p);
          }
        }
      }
    }

    char hide_cursor[] = "\x1b[?25l";
    char show_cursor[] = "\x1b[?25h";
    write(STDOUT_FILENO, hide_cursor, sizeof(hide_cursor));
    for (struct pane *p = lst; p; p = p->next) {
      pane_draw(p, false);
    }

    arrange(ws, lst);
    if (!focused) focused = lst;
    pane_focus(focused);
    write(STDOUT_FILENO, show_cursor, sizeof(show_cursor));

    {
      int i = 0;
      for (struct pane *p = lst; p; p = p->next) {
        fds[i + 1].fd = p->pty;
        fds[i + 1].events = POLL_OUT;
        i++;
      }
      nfds = 1 + i;
    }
  }
  exit_raw_mode();
  leave_alternate_screen();
  printf("[exited]\n");
}
