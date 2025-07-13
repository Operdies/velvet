#include "collections.h"
#include "utils.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static size_t next_size(size_t min) {
  return MAX(min * 2, 1024);
}

static void string_ensure_capacity(struct string *str, size_t required) {
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

void string_push_int(struct string *str, int n) {
  assert(n >= 0);
  const int max = 11;
  uint8_t buf[max];
  int idx = max;

  do {
    buf[--idx] = '0' + n % 10;
    n /= 10;
  } while (n);

  string_push_slice(str, buf + idx, max - idx);
}

void string_push_csi(struct string *str, int *ps, int n, const char *const c) {
  const uint8_t *csi = u8"\x1b[";
  string_push(str, csi);
  for (int i = 0; i < n; i++) {
    assert(ps[i] >= 0);
    if (ps[i]) string_push_int(str, ps[i]);
    string_push_char(str, ';');
  }
  if (n) { /* overwrite the last semicolon */
    str->len--;
  }
  string_push(str, (uint8_t *)c);
}

void string_push_slice(struct string *str, const uint8_t *const src, size_t len) {
  size_t required = str->len + len;
  string_ensure_capacity(str, required);
  memcpy(str->content + str->len, src, len);
  str->len += len;
}
void string_push(struct string *str, const uint8_t *src) {
  size_t len = strlen((char *)src);
  string_push_slice(str, src, len);
}

void string_push_char(struct string *str, uint8_t ch) {
  string_push_slice(str, &ch, 1);
}

void string_memset(struct string *str, uint8_t ch, size_t len) {
  size_t required = str->len + len;
  string_ensure_capacity(str, required);
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
      if (errno == EAGAIN || errno == EINTR) {
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

void vec_ensure_capacity(struct vec *v, size_t c) {
  assert(v->element_size && "Element size cannot be 0");
  if (v->capacity >= c) return;
  if (v->capacity <= 0) v->capacity = 100;
  while (v->capacity < c) {
    v->capacity *= 2;
    if (v->capacity <= 0) {
      die("Extremely large vector: 0x%x", c);
      v->capacity = c;
      break;
    }
  }
  if (v->content) {
    v->content = erealloc(v->content, v->capacity, v->element_size);
  } else {
    v->content = ecalloc(v->capacity, v->element_size);
  }
}

void vec_push(struct vec *v, const void *elem) {
  assert(v->element_size && "Element size cannot be 0");
  vec_ensure_capacity(v, v->length + 1);
  char *addr = (char *)v->content + v->length * v->element_size;
  if (elem)
    memcpy(addr, elem, v->element_size);
  else
    memset(addr, 0, v->element_size);
  v->length++;
}

void vec_clear(struct vec *v) {
  v->length = 0;
}

void vec_destroy(struct vec *v) {
  v->length = 0;
  v->capacity = 0;
  free(v->content);
  v->content = nullptr;
}

void running_hash_append(struct running_hash *hash, uint8_t ch) {
  hash->hash = (hash->hash >> 8) | (uint64_t)ch << 56;
}

bool running_hash_match(struct running_hash running, struct running_hash item, int count) {
  assert(count >= 1);
  assert(count <= 8);
  int discard = 8 * (8 - count);
  uint64_t keep = ~0UL >> discard;
  return (running.hash >> discard) == (item.hash & keep);
}
