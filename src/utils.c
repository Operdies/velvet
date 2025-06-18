#include "utils.h"
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

static void vflogmsg(FILE *f, char *fmt, va_list ap) {
  assert(f);
  assert(fmt);
  int n = strlen(fmt);
  char last = n > 0 ? fmt[n - 1] : 0;
  char secondTolast = n > 1 ? fmt[n - 2] : 0;

  vfprintf(f, fmt, ap);
  if (last != '\n') {
    fputc('\n', f);
  }

  // Ensure at least one space
  if (last == ':') {
    fprintf(f, " %s\n", strerror(errno));
  } else if (secondTolast == ':') {
    fprintf(f, "%s\n", strerror(errno));
  }
  fflush(f);
}

void *ecalloc(size_t sz, size_t count) {
  void *ptr = calloc(sz, count);
  if (!ptr) die("calloc:");
  return ptr;
}

_Noreturn void die(char *fmt, ...) {
  exit_raw_mode();
  va_list ap;
  va_start(ap);
  vflogmsg(stderr, fmt, ap);
  va_end(ap);
  exit(1);
}

#ifndef NDEBUG

static void vlogmsg(char *fmt, va_list ap) {
  static FILE *f;
  if (!f) {
    f = fopen("/tmp/vv.log", "w");
  }

  vflogmsg(f, fmt, ap);
}

void logmsg(char *fmt, ...) {
  va_list ap;
  va_start(ap);
  vlogmsg(fmt, ap);
  va_end(ap);
}
#else
void logmsg(char *fmt, ...) {
  (void)fmt;
}
#endif

struct termios original_terminfo;
struct termios raw_term;
struct winsize ws;

void leave_alternate_screen(void) {
  char buf[] = "\0330x1b[2J\033[H\033[?1049l";
  write(STDOUT_FILENO, buf, sizeof(buf));
}

void enter_alternate_screen(void) {
  char buf[] = "\033[?1049h\033[2J\033[H";
  write(STDOUT_FILENO, buf, sizeof(buf));
}

void exit_raw_mode(void) {
  char show_cursor[] = "\x1b[?25h";
  write(STDOUT_FILENO, show_cursor, sizeof(show_cursor));
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_terminfo);
}

void enable_raw_mode(void) {
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
    die("ioctl TIOCGWINSZ:");
  }

  // Save and configure raw terminal mode
  if (tcgetattr(STDIN_FILENO, &original_terminfo) == -1) {
    die("tcgetattr:");
  }

  raw_term = original_terminfo;
  cfmakeraw(&raw_term);
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw_term) == -1) {
    die("tcsetattr:");
  }
}


