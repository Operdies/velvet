#ifndef COLLECTIONS_H
#define COLLECTIONS_H

#ifdef MIN
#undef MIN
#undef MAX
#endif

#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define MAX(x, y) ((x) > (y) ? (x) : (y))

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define LENGTH(x) (sizeof(x) / sizeof((x)[0]))
#define INT_SLICE(...) ((struct int_slice) { .n = LENGTH(((int[]){ __VA_ARGS__ })), .content = ((int[]){ __VA_ARGS__ }) })

struct int_slice {
  int *content;
  int n;
};

struct string {
  uint8_t *content;
  size_t len, cap;
};

struct string_slice {
  size_t len;
  const uint8_t *const content;
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
#ifndef RELEASE_BUILD
  const char *typename;
#endif
};

struct running_hash {
  union {
    uint8_t characters[8];
    uint64_t hash;
  };
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

bool running_hash_match(struct running_hash running, struct running_hash item,
                        int count);
void running_hash_append(struct running_hash *hash, uint8_t ch);
void string_push_slice(struct string *str, struct string_slice slice);
void string_push_range(struct string *str, const uint8_t *const src,
                       size_t len);
void string_push(struct string *str, const uint8_t *const src);
void string_push_char(struct string *str, uint8_t ch);
void string_push_int(struct string *str, int value);
void string_memset(struct string *str, uint8_t ch, size_t len);
void string_clear(struct string *str);
void string_destroy(struct string *str);
/* flush the string instance to the specified file descriptor */
bool string_flush(struct string *str, int fd, int *total_written);
void string_push_csi(struct string *str, struct int_slice params, const char *const c);

void vec_push(struct vec *v, const void *elem);
/* remove the nth element from the vector */
void vec_remove(struct vec *v, size_t n);
/* swap two vector elements */
void vec_swap(struct vec *v, size_t i, size_t j);
void vec_clear(struct vec *v);
void vec_destroy(struct vec *v);
/* Append a zero'd out structure to the vector and return a pointer to it */
void *vec_new_element(struct vec *v);

#ifdef RELEASE_BUILD
#define vec(type) (struct vec) { .element_size = sizeof(type) }
#else
#define vec(type) (struct vec) { .element_size = sizeof(type), .typename = #type }
#endif

#define vec_nth(vec, n)                                                        \
  (void *)((char *)(vec).content + ((n) * (vec).element_size))
#define vec_foreach(item, vec)                                                 \
  assert(sizeof(*(item)) == (vec).element_size);                               \
  for ((item) = vec.content;                                                   \
       ((char *)(item)) <                                                      \
       ((char *)(vec).content + (vec).length * (vec).element_size);            \
       (item)++)

#endif /*  COLLECTIONS_H */
