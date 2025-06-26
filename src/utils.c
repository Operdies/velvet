#include "utils.h"
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

static inline void leave_alternate_screen(void);
static inline void enter_alternate_screen(void);
static inline void exit_raw_mode(void);
static inline void enable_raw_mode(void);
static inline void enable_focus_reporting(void);
static inline void disable_focus_reporting(void);

#define BUFSIZE 128
static void vflogmsg(FILE *f, char *fmt, va_list ap) {
  static char prevbuf[BUFSIZE] = {0};
  static char buf[BUFSIZE] = {0};
  static int repeat_count = 0;
  assert(f);
  assert(fmt);
  int n = strlen(fmt);
  char last = n > 0 ? fmt[n - 1] : 0;

  int n_buf = vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
  if (n_buf > BUFSIZE)
    n_buf = BUFSIZE;

  // Ensure at least one space
  if (last == ':') {
    n_buf += snprintf(buf + n_buf, sizeof(buf) - n_buf, " %s", strerror(errno));
  }

  for (int i = 0; i < n_buf; i++) {
    // Assume utf8 continuation byte
    if ((buf[i]) & 0x80) continue;
    if (buf[i] == ' ') continue;
    if (!isgraph(buf[i])) buf[i] = '.';
  }
  if (strncmp(buf, prevbuf, n_buf) != 0) {
    repeat_count = 1;
    fprintf(f, "\n%.*s", n_buf, buf);
    memcpy(prevbuf, buf, n_buf);
  } else {
    repeat_count++;
    fprintf(f, "\r%.*s (%d)", n_buf, buf, repeat_count);
  }
  fflush(f);
}

void flogmsg(FILE *f, char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vflogmsg(f, fmt, ap);
  va_end(ap);
}

void *ecalloc(size_t sz, size_t count) {
  void *ptr = calloc(sz, count);
  if (!ptr) die("calloc:");
  return ptr;
}

_Noreturn void die(char *fmt, ...) {
  exit_raw_mode();
  va_list ap;
  va_start(ap, fmt);
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
  va_start(ap, fmt);
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
  char buf[] = "\x1b[2J\x1b[H\x1b[?1049l";
  write(STDOUT_FILENO, buf, sizeof(buf));
}

void enter_alternate_screen(void) {
  char buf[] = "\x1b[?1049h\x1b[2J\x1b[H";
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


static void disable_line_wrapping(void) {
  char *disable = "\x1b[?7l";
  write(STDOUT_FILENO, disable, strlen(disable));
}
static void enable_line_wrapping(void) {
  char *disable = "\x1b[?7h";
  write(STDOUT_FILENO, disable, strlen(disable));
}

static void disable_focus_reporting(void) {
  char buf[] = "\x1b[?1004l";
  write(STDOUT_FILENO, buf, sizeof(buf));
}

static void enable_focus_reporting(void) {
  // dprintf(STDOUT_FILENO, "\x1b[1004h");
  char buf[] = "\x1b[?1004h";
  write(STDOUT_FILENO, buf, sizeof(buf));
}

void enable_raw_mode_etc(void) {
  enter_alternate_screen();
  enable_raw_mode();
  disable_line_wrapping();
  enable_focus_reporting();
}
void disable_raw_mode_etc(void) {
  disable_focus_reporting();
  enable_line_wrapping();
  exit_raw_mode();
  leave_alternate_screen();
}
