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
#include "collections.h"

#define CSI "\x1b["

#define MURMUR3_SEED (123) // doesn't matter

static void vflogmsg(FILE *f, char *fmt, va_list ap) {
  assert(f);
  assert(fmt);
  static struct string str = {0};
  static uint32_t prev_hash = 0;
  static int repeat_count = 0;

  string_push_vformat_slow(&str, fmt, ap);

  // Ensure at least one space
  int n = strlen(fmt);
  if (n && fmt[n-1] == ':') {
    string_push_format_slow(&str, " %s (%d)", strerror(errno), errno);
  }

#define REPLACEMENTS_LST                                                                                               \
  X("\x1b[", "<CSI>")                                                                                                  \
  X("\x1b", "<ESC>")                                                                                                   \
  X("\t", "<TAB>")

#define X(old, new) string_replace_inplace_slow(&str, old, new);
  REPLACEMENTS_LST

  for (int i = 'a' & 0x1f; i < ('z' & 0x1f); i++) {
    char old[] = { i, 0 };
    char new[] = { '<', 'C', '-', i + 96, '>', 0 };
    string_replace_inplace_slow(&str, old, new);
  }

  time_t rawtime;
  struct tm *timeinfo;
  time(&rawtime);
  timeinfo = localtime(&rawtime);
  char timebuf[12] = {0};
  strftime(timebuf, sizeof(timebuf), "[%H:%M:%S]", timeinfo);

  uint32_t buf_hash = murmur3_32((uint8_t*)str.content, str.len, MURMUR3_SEED);
  bool repeat = buf_hash == prev_hash;
  prev_hash = buf_hash;
  char *final_fmt = repeat ? ( CSI "F" CSI "2K" "%s %.*s (%d)\n" ) : ( "%s %.*s\n" );
  repeat_count = repeat ? repeat_count + 1 : 1;
  fprintf(f, final_fmt, timebuf, str.len, str.content, repeat_count);
  fflush(f);
  string_clear(&str);
}

void *velvet_erealloc(void *array, size_t nmemb, size_t size) {
  void *p;

  if (!(p = realloc(array, nmemb * size))) velvet_die("realloc:");

  return p;
}

void *velvet_calloc(size_t sz, size_t count) {
  void *ptr = calloc(sz, count);
  if (!ptr) velvet_die("calloc:");
  return ptr;
}

_Noreturn void velvet_die(char *fmt, ...) {
  terminal_reset();
  va_list ap;
  va_start(ap, fmt);
  vflogmsg(stderr, fmt, ap);
  va_end(ap);
  __builtin_trap();
}

_Noreturn void velvet_fatal(char *fmt, ...) {
  terminal_reset();
  va_list ap;
  va_start(ap, fmt);
  vflogmsg(stderr, fmt, ap);
  va_end(ap);
  exit(1);
}

#ifndef NDEBUG

static void vlogmsg(char *fmt, va_list ap) {
  vflogmsg(stdout, fmt, ap);
}

void velvet_log(char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vlogmsg(fmt, ap);
  va_end(ap);
}
#else
void velvet_log(char *fmt, ...) {
  (void)fmt;
}
#endif

