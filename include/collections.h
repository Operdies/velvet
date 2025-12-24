#ifndef COLLECTIONS_H
#define COLLECTIONS_H

#include "text.h"
#ifdef MIN
#undef MIN
#undef MAX
#endif

#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define CLAMP(x, low, high) (MIN(high, MAX(x, low)))

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define LENGTH(x) ((int)(sizeof(x) / sizeof((x)[0])))
#define INT_SLICE(...) ((struct int_slice) { .n = LENGTH(((int[]){ __VA_ARGS__ })), .content = ((int[]){ __VA_ARGS__ }) })
#define STRING_SLICE(slc) ((struct u8_slice) { .len = sizeof(slc) - 1, .content = slc })

struct int_slice {
  int *content;
  size_t n;
};

struct string {
  uint8_t *content;
  size_t len, cap;
};

struct u8_slice {
  size_t len;
  const uint8_t *content;
};

struct u8_slice_codepoint_iterator {
  struct u8_slice src;
  size_t cursor;
  struct codepoint current;
};

// A view into a vector
struct vec_slice {
  const size_t length;
  const void *content;
  const size_t element_size;
};

// generically sized vector
struct vec {
  size_t length;
  void *content;
  size_t element_size;
  size_t capacity;
#ifndef RELEASE_BUILD
  const char *typename;
#endif
};

enum [[gnu::packed]] hashmap_slot_state {
  // Never used
  HASHMAP_SLOT_PRISTINE,
  // Currently used
  HASHMAP_SLOT_OCCUPIED,
  // Previously used
  HASHMAP_SLOT_TOMBSTONE
};

struct hashmap {
  uint32_t capacity;
  uint32_t count;
  uint32_t *keys;
  void **values;
  enum hashmap_slot_state *metadata;
};

// Destroy
void hashmap_destroy(struct hashmap *h);
// Add an item if the specified key does not exist.
bool hashmap_add(struct hashmap *h, uint32_t key, void *item);
// Set the item with the specified key, returning the existing item.
void *hashmap_set(struct hashmap *h, uint32_t key, void *item);
// Return a bool indicating if a given key exists.
bool hashmap_contains(const struct hashmap *h, uint32_t key);
// Get the item with the specified key, if it exists.
void *hashmap_get(const struct hashmap *h, uint32_t key);
// Return the item with the specified key, if it exists, returning it.
bool hashmap_remove(struct hashmap *h, uint32_t key, void **value);
int string_replace_inplace_slow(struct string *str, const char *const old, const char *const new);
void string_push_cstr(struct string *str, char *cstr);
void string_push_string(struct string *dest, struct string src);
void string_push_slice(struct string *str, struct u8_slice slice);
void string_push_range(struct string *str, const uint8_t *const src,
                       size_t len);
void string_push(struct string *str, const uint8_t *const src);
void string_push_char(struct string *str, uint8_t ch);
void string_push_int(struct string *str, int value);
void string_memset(struct string *str, uint8_t ch, size_t len);
void string_clear(struct string *str);
void string_destroy(struct string *str);
void string_push_csi(struct string *str, uint8_t leading, struct int_slice params, const char *const final);
bool string_starts_with(struct string *str, struct u8_slice slice);
bool string_ends_with(struct string *str, struct u8_slice slice);
void string_drop_left(struct string *str, size_t n);
/* truncate `v` to size `len`. If `len` is greater than the current size,
 * `v` will be resized and elements zero'd */
void vec_truncate(struct vec *v, size_t len);
/* insert `elem` and index `i` and move succeeding elements as needed. */
void vec_insert(struct vec *v, size_t i, const void *elem);
/* set the `ith` element of `v` to `elem`. If `i` is out of range, resize `v` to accomodate it. */
void vec_set(struct vec *v, size_t i, const void *elem);
/* push `count` elements from `elems` to `v` */
void vec_push_range(struct vec *v, const void *elems, size_t count);
/* push `elem` to `v` */
void vec_push(struct vec *v, const void *elem);
/* remove the element represented by `e` from the vector by swapping with the last element.
 * This gives us constant time removal at the cost of not preserving order. */
void vec_swap_remove(struct vec *v, void *e);
/* remove the last element from the vector and return it */
void *vec_pop(struct vec *v);
/* remove the element represented by `e` from the vector */
void vec_remove(struct vec *v, void *e);
/* remove the nth element from the vector */
void vec_remove_at(struct vec *v, size_t n);
/* swap two vector elements */
void vec_swap(struct vec *v, size_t i, size_t j);
void vec_clear(struct vec *v);
void vec_destroy(struct vec *v);
/* Append a zero'd out structure to the vector and return a pointer to it */
void *vec_new_element(struct vec *v);
void *vec_nth_unchecked(const struct vec *const v, size_t i);
void *vec_nth(const struct vec *const v, size_t i);
struct u8_slice string_as_u8_slice(struct string s);
struct u8_slice u8_slice_from_cstr(const char *const str);
struct u8_slice u8_slice_range(struct u8_slice s, ssize_t start, ssize_t end);
bool u8_slice_starts_with_cstr(struct u8_slice slice, char *str);
bool u8_slice_starts_with(struct u8_slice slice, struct u8_slice prefix);
bool u8_slice_equals(struct u8_slice a, struct u8_slice b);
bool u8_slice_equals_ignore_case(struct u8_slice a, struct u8_slice b);
bool u8_slice_contains(struct u8_slice s, uint8_t ch);
struct u8_slice u8_slice_strip(struct u8_slice s, struct u8_slice chars);
struct u8_slice u8_slice_strip_whitespace(struct u8_slice s);
struct u8_slice u8_slice_strip_quotes(struct u8_slice s);
struct u8_slice string_range(const struct string *const s, ssize_t start, ssize_t end);
ssize_t vec_index(struct vec *v, const void *const item);
void string_push_vformat_slow(struct string *s, char *fmt, va_list ap);
void string_push_format_slow(struct string *s, char *fmt, ...) __attribute__((format(printf, 2, 3)));
size_t u8_slice_strlen(struct u8_slice s);
size_t string_strlen(struct string s);
void string_ensure_null_terminated(struct string *s);
bool u8_slice_codepoint_iterator_next(struct u8_slice_codepoint_iterator *s);

#ifdef RELEASE_BUILD
#define vec(type) (struct vec) { .element_size = sizeof(type) }
#else
#define vec(type) (struct vec) { .element_size = sizeof(type), .typename = #type }
#endif

#define vec_rforeach(item, vec)                                                                                        \
  assert(sizeof(*(item)) == (vec).element_size);                                                                       \
  for ((item) = ((vec).length == 0)                                                                                    \
                    ? nullptr                                                                                          \
                    : (void *)((char *)(vec).content + (vec).length * (vec).element_size - (vec).element_size);        \
       ((vec).length) && (((char *)(item)) >= (char *)(vec).content);                                                  \
       (item)--)

#define vec_rwhere(item, vec, cond) vec_rforeach(item, vec) if ((cond))

#define vec_foreach(item, vec)                                                                                         \
  assert(sizeof(*(item)) == (vec).element_size);                                                                       \
  for ((item) = (vec).content; (((char *)(item)) < ((char *)(vec).content + (vec).length * (vec).element_size)); (item)++)

#define vec_where(item, vec, cond) vec_foreach(item, vec) if ((cond))

#define vec_find(item, vec, cond)                                                                                      \
  do {                                                                                                                 \
    assert(sizeof(*(item)) == (vec).element_size);                                                                     \
    item = nullptr;                                                                                                    \
    if ((vec).length == 0) break;                                                                                      \
    for ((item) = vec.content; (item) && !(cond);) {                                                                   \
      item++; /* go next */                                                                                            \
      if (!(((char *)(item)) < ((char *)(vec).content + (vec).length * (vec).element_size))) {                         \
        item = nullptr; /* set item to nullptr if not found */                                                         \
        break;                                                                                                         \
      }                                                                                                                \
    }                                                                                                                  \
  } while (0)

#endif /*  COLLECTIONS_H */
