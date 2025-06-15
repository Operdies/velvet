#include "utils.h"
#include <assert.h>
#include <string.h>

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

void die(char *fmt, ...) {
  va_list ap;
  va_start(ap);
  vflogmsg(stderr, fmt, ap);
  va_end(ap);
  exit(1);
}

void *ecalloc(size_t sz, size_t count) {
  void *ptr = calloc(sz, count);
  if (!ptr) die("calloc:");
  return ptr;
}
