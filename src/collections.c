#include "collections.h"
#include "utils.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static size_t next_size(size_t min) {
  return MAX(min * 2, 1024);
}

static void ensure_capacity(struct string *str, size_t required) {
  if (str->content == nullptr || str->cap < required) {
    uint8_t *prev = str->content;
    size_t newsize = next_size(required);
    str->content = ecalloc(newsize, 1);
    str->cap = newsize;
    if (prev && str->len) {
      memcpy(str->content, prev, str->len);
      free(prev);
    }
  }
}

void string_push(struct string *str, char *src, size_t len) {
  size_t required = str->len + len;
  ensure_capacity(str, required);
  memcpy(str->content + str->len, src, len);
  str->len += len;
}

void string_memset(struct string *str, uint8_t ch, size_t len) {
  size_t required = str->len + len;
  ensure_capacity(str, required);
  memset(str->content + str->len, ch, len);
  str->len += len;
}

void string_clear(struct string *str) {
  str->len = 0;
}

void string_destroy(struct string *str) {
  free(str->content);
}

bool string_flush(struct string *str, int fd, int *total_written) {
  size_t written = 0;
  while (written < str->len) {
    ssize_t w = write(fd, str->content + written, str->len - written);
    if (w == -1) {
      if (errno == EAGAIN) {
        usleep(1);
        continue;
      }
      die("write:");
    }
    if (w == 0) {
      if (total_written) *total_written = written;
      string_clear(str);
      return true;
    }
    written += w;
  }
  if (total_written) *total_written = written;
  string_clear(str);
  return true;
}
