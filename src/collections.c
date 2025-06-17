#include "collections.h"
#include "utils.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static size_t next_size(size_t min) {
  return MAX(min * 2, 1024);
}

void string_push(struct string *str, char *src, size_t len) {
  size_t required = str->len + len;
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
  memcpy(str->content + str->len, src, len);
  str->len += len;
}

void string_clear(struct string *str) {
  str->len = 0;
}

void string_destroy(struct string *str) {
  free(str->content);
}
