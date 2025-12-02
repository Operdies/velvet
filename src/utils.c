#include "utils.h"
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <platform.h>
#include "murmur3.h"

#define CSI "\x1b["

#define MURMUR3_SEED (123) // doesn't matter

static void vflogmsg(FILE *f, char *fmt, va_list ap) {
  constexpr int bufsize = 1 << 16;
  static char *buf = nullptr;
  if (!buf) {
    buf = calloc(1, bufsize);
  }
  static uint32_t prev_hash = 0;

  static int repeat_count = 0;
  assert(f);
  assert(fmt);
  int n = strlen(fmt);
  char last = n > 0 ? fmt[n - 1] : 0;

  int n_buf = vsnprintf(buf, bufsize - 1, fmt, ap);
  if (n_buf > bufsize) n_buf = bufsize;

  // Ensure at least one space
  if (last == ':') {
    n_buf += snprintf(buf + n_buf, bufsize - n_buf, " %s (%d)", strerror(errno), errno);
  }

  // replace non-printable characters with dots
  for (int i = 0; i < n_buf; i++) {
    // Assume utf8 continuation byte
    if ((buf[i]) & 0x80) continue;
    if (buf[i] == ' ') continue;
    if (buf[i] == '\\') buf[i] = '.';
    if (!isgraph(buf[i])) buf[i] = '.';
  }

  time_t rawtime;
  struct tm *timeinfo;
  time(&rawtime);
  timeinfo = localtime(&rawtime);
  char timebuf[12] = {0};
  strftime(timebuf, sizeof(timebuf), "[%H:%M:%S]", timeinfo);

  uint32_t buf_hash = murmur3_32((uint8_t*)buf, bufsize, MURMUR3_SEED);
  bool repeat = buf_hash == prev_hash;
  prev_hash = buf_hash;
  char *final_fmt = repeat ? ( CSI "F" CSI "2K" "%s %.*s (%d)\n" ) : ( "%s %.*s\n" );
  repeat_count = repeat ? repeat_count + 1 : 1;
  fprintf(f, final_fmt, timebuf, n_buf, buf, repeat_count);
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
  terminal_reset();
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

