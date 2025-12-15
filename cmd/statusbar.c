#include "utils.h"
#include <poll.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>

#define CSI "\x1b["

static bool focused = true;
static void render(void) {
  char buf[100];
  static int offset = 0;
  int n = 0;
  if (focused) {
    offset = (offset + 1) % 4;
    n = snprintf(buf, sizeof(buf), CSI "2K" CSI "1;1H > Active%.*s", offset, "......");
  } else {
    n = snprintf(buf, sizeof(buf), CSI "2K" CSI "1;1H > Inactive");
  }
  write(STDOUT_FILENO, buf, n);
}

int main(void) {
  terminal_setup();
  struct pollfd fds[1] = {0};
  fds[0].fd = STDIN_FILENO;
  fds[0].events = POLLIN;

  char readbuf[4096];
  bool running = true;

  for (; running;) {
    render();
    int n = poll(fds, 1, 1000);
    if (n == -1) {
      velvet_die("poll:");
    }
    if (n) {
      int n_read = read(STDIN_FILENO, readbuf, sizeof(readbuf));
      for (int i = 0; i < n; i++) {
        if (readbuf[i] == CTRL('W')) running = false;
      }
      if (n_read > 2 && readbuf[0] == 0x1b && readbuf[1] == '[') {
        if (readbuf[2] == 'O') {
          focused = false;
        } else if (readbuf[2] == 'I') {
          focused = true;
        }
      }
    }
  }
  terminal_reset();
}
