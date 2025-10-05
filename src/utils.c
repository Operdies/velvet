#include "utils.h"
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <platform.h>


static void vflogmsg(FILE *f, char *fmt, va_list ap) {
  constexpr int bufsize = 256;
  static char prevbuf[bufsize] = {0};
  static char buf[bufsize] = {0};
  static int repeat_count = 0;
  assert(f);
  assert(fmt);
  int n = strlen(fmt);
  char last = n > 0 ? fmt[n - 1] : 0;

  int n_buf = vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
  if (n_buf > bufsize) n_buf = bufsize;

  // Ensure at least one space
  if (last == ':') {
    n_buf += snprintf(buf + n_buf, sizeof(buf) - n_buf, " %s", strerror(errno));
  }

  for (int i = 0; i < n_buf; i++) {
    // Assume utf8 continuation byte
    if ((buf[i]) & 0x80) continue;
    if (buf[i] == ' ') continue;
    if (buf[i] == '\\') buf[i] = '.';
    if (!isgraph(buf[i])) buf[i] = '.';
  }

  if (strcmp(buf, prevbuf) == 0) {
    repeat_count++;
    fprintf(f, "\r%.*s (%d)", n_buf, buf, repeat_count);
  } else {
    repeat_count = 1;
    fprintf(f, "\n%.*s", n_buf, buf);
    memcpy(prevbuf, buf, n_buf + 1);
  }
  fflush(f);
}

void flogmsg(FILE *f, char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vflogmsg(f, fmt, ap);
  va_end(ap);
}

void *erealloc(void *array, size_t nmemb, size_t size) {
  void *p;

  if (!(p = realloc(array, nmemb * size))) die("realloc:");

  return p;
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

