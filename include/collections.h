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

int string_replace_inplace_slow(struct string *str, const char *const old, const char *const new);
struct u8_slice u8_slice_from_string(struct string s);
void string_push_cstr(struct string *str, const char *cstr);
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
void string_shift_left(struct string *str, size_t n);
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
/* sort the vector in-place according to a comparison function */
void vec_sort(struct vec *v, int (*cmp)(const void *, const void *));
/* find the index of `elem`. If the element does not exist,
 * returns the complement of the index where it would be. This function assumes the vector is sorted according to `cmp`. */
ssize_t vec_binsearch(struct vec *v, const void *elem, int (*cmp)(const void *, const void *));
/* Append a zero'd out structure to the vector and return a pointer to it */
void *vec_new_element(struct vec *v);
void *vec_nth_unchecked(struct vec v, size_t i);
void *vec_nth(struct vec v, size_t i);
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
int u8_slice_codepoint_iterator_length(struct u8_slice_codepoint_iterator s);
void vec_shift_left(struct vec *v, size_t n);
bool u8_slice_to_int32(struct u8_slice s, int *i32);
bool u8_match(struct u8_slice s, char *opt);

#ifdef RELEASE_BUILD
#define vec(type) (struct vec) { .element_size = sizeof(type) }
#else
#define vec(type) (struct vec) { .element_size = sizeof(type), .typename = #type }
#endif

#define vec_rforeach(item, vec)                                                                                        \
  assert(sizeof(*(item)) == (vec).element_size);                                                                       \
  for ((item) = ((vec).length == 0)                                                                                    \
                    ? NULL                                                                                          \
                    : (void *)((char *)(vec).content + (vec).length * (vec).element_size - (vec).element_size);        \
       ((vec).length) && (((char *)(item)) >= (char *)(vec).content);                                                  \
       (item)--)

#define vec_rwhere(item, vec, cond) vec_rforeach(item, vec) if ((cond))

#define vec_foreach(item, vec)                                                                                         \
  assert((vec).length == 0 || sizeof(*(item)) == (vec).element_size);                                                  \
  for ((item) = (vec).content; (((char *)(item)) < ((char *)(vec).content + (vec).length * (vec).element_size));       \
       (item)++)

#define vec_where(item, vec, cond) vec_foreach(item, vec) if ((cond))

#define vec_find(item, vec, cond)                                                                                      \
  do {                                                                                                                 \
    assert(sizeof(*(item)) == (vec).element_size);                                                                     \
    item = NULL;                                                                                                    \
    if ((vec).length == 0) break;                                                                                      \
    for ((item) = vec.content; (item) && !(cond);) {                                                                   \
      item++; /* go next */                                                                                            \
      if (!(((char *)(item)) < ((char *)(vec).content + (vec).length * (vec).element_size))) {                         \
        item = NULL; /* set item to NULL if not found */                                                         \
        break;                                                                                                         \
      }                                                                                                                \
    }                                                                                                                  \
  } while (0)

#endif /*  COLLECTIONS_H */
