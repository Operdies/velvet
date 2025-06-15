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

#include "utils.h"
#include "pane.h"

#define MAX_LINES 100
#define MAX_LINE_LENGTH 100

static struct pane *focused = NULL;

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

static void arrange(struct winsize ws, struct pane *panes) {
  int mh, sh, mx, mw, my, sy, sw, nm, ns, i, n;

  n = 0;
  for (struct pane *p = panes; p; p = p->next) n++;

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
    struct pane *p = &panes[i];
    p->x = mx;
    p->y = my;
    my += mh;
    pane_resize(p, mw, mh);
  }

  for (; i < n; i++) {
    struct pane *p = &panes[i];
    p->x = mw;
    p->y = sy;
    sy += sh;
    pane_resize(p, sw, sh);
  }
}

static void leavealternatescreen(void) {
  char buf[] = "\0330x1b[2J\033[H\033[?1049l";
  write(STDOUT_FILENO, buf, sizeof(buf));
}

static void enteralternatescreen(void) {
  char buf[] = "\033[?1049h\033[2J\033[H";
  write(STDOUT_FILENO, buf, sizeof(buf));
}

struct termios original_terminfo;

static void restore_terminfo() {
  leavealternatescreen();
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_terminfo);
}

int main(int argc, char **argv) {
  struct termios raw_term;
  struct winsize ws;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
    die("ioctl TIOCGWINSZ:");
  }

  // Save and configure raw terminal mode
  if (tcgetattr(STDIN_FILENO, &original_terminfo) == -1) {
    die("tcgetattr:");
  }
  atexit(restore_terminfo);

  raw_term = original_terminfo;
  cfmakeraw(&raw_term);
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw_term) == -1) {
    die("tcsetattr:");
  }
  enteralternatescreen();

  install_signal_handlers();

  struct pane *lst = NULL;
  {
    struct pane *prev = lst;
    for (int i = 1; i < argc; i++) {
      struct pane *p = calloc(1, sizeof(*p));
      p->process = strdup(argv[i]);
      p->next = lst;
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

  char buf[4096];
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
      int n = read(STDIN_FILENO, buf, sizeof(buf));
      if (n == 0) {
        break;
      }
      for (int i = 0; i < n; i++) {
        if (buf[i] == CTRL('W')) {
          logmsg("Exit by ^W");
          exit(0);
        }
      }
      write(focused->pty, buf, n);
      if (n > 0) {
        logmsg("stdin: %d %.*s", n, n, buf);
      }
      if (n == -1) {
        die("read:");
      }
    }

    {
      for (int i = 1; i < nfds; i++) {
        if (fds[i].revents & POLL_OUT) {
          struct pane *p = pane_from_pty(lst, fds[i].fd);
          logmsg("[%d] Read from %s", p->pty, p->process);
          if (p) {
            bool exit = false;
            pane_read(p, &exit);
            if (exit) {
              pane_remove(&lst, p);
            }
          }
        }
      }
    }

    arrange(ws, lst);
    // TODO: Move cursor to correct cell in focused pane

    {
      int i = 0;
      for (struct pane *p = lst; p; p = p->next) {
        fds[i + 1].fd = p->pty;
        fds[i = 1].events = POLL_OUT;
        i++;
      }
      nfds = 1 + i;
    }
  }
  restore_terminfo();
  printf("[exited]\n");
}
