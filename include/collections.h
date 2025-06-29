#ifndef COLLECTIONS_H
#define COLLECTIONS_H

#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define MAX(x, y) ((x) > (y) ? (x) : (y))

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* TODO:
 * String prepend without copy:
 * Write data to end of buffer
 * On resize: Cannonicalize string by copying backbuffer
 * On flush: write with iovec
 */
struct string {
  uint8_t *content;
  size_t len, cap;
};

struct string_slice {
  size_t len;
  const char *const str;
};

// A view into a vector
struct vec_slice {
  const size_t length;
  const void *content;
  const size_t element_size;
};

// generically sized vector
struct vec {
  union {
    struct vec_slice slice;
    struct {
      size_t length;
      void *content;
      size_t element_size;
    };
  };
  size_t capacity;
};

void string_push_slice(struct string *str, const uint8_t *const src, size_t len);
void string_push(struct string *str, const char *const src);
void string_push_char(struct string *str, char ch);
void string_push_int(struct string *str, int value);
void string_memset(struct string *str, uint8_t ch, size_t len);
void string_clear(struct string *str);
void string_destroy(struct string *str);
/* flush the string instance to the specified file descriptor */
bool string_flush(struct string *str, int fd, int *total_written);

void vec_push(struct vec *v, const void *elem);
void vec_clear(struct vec *v);
void vec_destroy(struct vec *v);

#define vec(type) (struct vec){.element_size = sizeof(type)};
#define vec_nth(vec, n)                                                        \
  (void *)((char *)(vec).content + ((n) * (vec).element_size))
#define vec_foreach(item, vec)                                                 \
  assert(sizeof(*(item)) == (vec).element_size);                               \
  for ((item) = vec.content;                                                   \
       ((char *)(item)) <                                                      \
       ((char *)(vec).content + (vec).length * (vec).element_size);            \
       (item)++)

#endif /*  COLLECTIONS_H */
