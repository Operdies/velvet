#include "platform.h"
#include <ctype.h>
#include <pty_host.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "collections.h"
#include "vte.h"
#include "utils.h"

extern pid_t forkpty(int *, char *, struct termios *, struct winsize *);

void pty_host_destroy(struct pty_host *pty_host) {
  if (pty_host->pty > 0) {
    close(pty_host->pty);
    pty_host->pty = 0;
  }
  if (pty_host->pid > 0) {
    int status;
    kill(pty_host->pid, SIGTERM);
    pid_t result = waitpid(pty_host->pid, &status, WNOHANG);
    if (result == -1) velvet_die("waitpid:");
  }
  vte_destroy(&pty_host->emulator);
  string_destroy(&pty_host->cmdline);
  string_destroy(&pty_host->title);
  string_destroy(&pty_host->icon);
  string_destroy(&pty_host->cwd);
  pty_host->pty = pty_host->pid = 0;
}

void pty_host_update_cwd(struct pty_host *p) {
  if (p->pty && platform.get_cwd_from_pty) {
    char buf[256] = {0};
    if (platform.get_cwd_from_pty(p->pty, buf, sizeof(buf))) {
      string_clear(&p->cwd);
      string_push_cstr(&p->cwd, buf);
      string_clear(&p->title);
      string_push_string(&p->title, p->cmdline);
      string_push_cstr(&p->title, " in ");
      string_push_string(&p->title, p->cwd);

      char *home = getenv("HOME");
      if (home) string_replace_inplace_slow(&p->title, home, "~");
    }
  } else if (!p->title.len && p->cmdline.len) {
    // fallback to using the process as title
    string_clear(&p->title);
    string_push_string(&p->title, p->cmdline);
  }
}


void pty_host_process_output(struct pty_host *pty_host, struct u8_slice str) {
  // Pass current size information to vte so it can determine if screens should be resized
  vte_set_size(&pty_host->emulator, pty_host->rect.client.columns, pty_host->rect.client.lines);
  vte_process(&pty_host->emulator, str);
}

void pty_host_resize(struct pty_host *pty_host, struct bounds outer) {
  // Refuse to go below a minimum size
  if (outer.columns < 2) outer.columns = 2;
  if (outer.lines < 2) outer.lines = 2;

  int pixels_per_column = (int)((float)outer.x_pixel / (float)outer.columns);
  int pixels_per_row = (int)((float)outer.y_pixel / (float)outer.lines);

  bool leftmost = outer.x == 0;

  struct bounds inner = (struct bounds){.x = outer.x + (leftmost ? 0 : pty_host->border_width),
                                        .y = outer.y + pty_host->border_width,
                                        .columns = outer.columns - (leftmost ? 0 : pty_host->border_width),
                                        .lines = outer.lines - pty_host->border_width};
  inner.x_pixel = inner.columns * pixels_per_column;
  inner.y_pixel = inner.lines * pixels_per_row;

  if (pty_host->rect.window.columns != outer.columns || pty_host->rect.window.lines != outer.lines) {
    struct winsize ws = {.ws_col = inner.columns, .ws_row = inner.lines, .ws_xpixel = inner.x_pixel, .ws_ypixel = inner.y_pixel};
    if (pty_host->pty) ioctl(pty_host->pty, TIOCSWINSZ, &ws);
    if (pty_host->pid) kill(pty_host->pid, SIGWINCH);
  }

  pty_host->rect.window = outer;
  pty_host->rect.client = inner;

  vte_set_size(&pty_host->emulator, pty_host->rect.client.columns, pty_host->rect.client.lines);
}

void pty_host_start(struct pty_host *pty_host) {
  struct winsize pty_hostsize = {
      .ws_col = pty_host->rect.client.columns,
      .ws_row = pty_host->rect.client.lines,
      .ws_xpixel = pty_host->rect.client.x_pixel,
      .ws_ypixel = pty_host->rect.client.y_pixel,
  };
  pid_t pid = forkpty(&pty_host->pty, NULL, NULL, &pty_hostsize);
  if (pid < 0) velvet_die("forkpty:");

  if (pid == 0) {
    string_ensure_null_terminated(&pty_host->cmdline);
    char *argv[] = {"sh", "-c", (char*)pty_host->cmdline.content, NULL};
    execvp("sh", argv);
    velvet_die("execlp:");
  }
  pty_host->pid = pid;
  set_nonblocking(pty_host->pty);
}
