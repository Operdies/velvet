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

  string_push_range(str, buf + idx, max - idx);
}

void string_push_csi(struct string *str, struct int_slice params, const char *const c) {
  const uint8_t *csi = u8"\x1b[";
  string_push(str, csi);
  for (int i = 0; i < params.n; i++) {
    assert(params.content[i] >= 0);
    if (i) string_push_char(str, ';');
    if (params.content[i]) string_push_int(str, params.content[i]);
  }
  string_push(str, (uint8_t *)c);
}

void string_push_slice(struct string *str, struct string_slice slice) {
  string_push_range(str, slice.content, slice.len);
}

void string_push_range(struct string *str, const uint8_t *const src, size_t len) {
  size_t required = str->len + len;
  string_ensure_capacity(str, required);
  memcpy(str->content + str->len, src, len);
  str->len += len;
}
void string_push(struct string *str, const uint8_t *src) {
  size_t len = strlen((char *)src);
  string_push_range(str, src, len);
}

void string_push_char(struct string *str, uint8_t ch) {
  string_push_range(str, &ch, 1);
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
  str->cap = str->len = 0;
  free(str->content);
  str->content = nullptr;
}

// TODO: Avoid blocking here.
// If the full content cannot be flushed (EAGAIN), try again later
// This requires moving the content of the *str object instead of clearing it.
// Special care should be taken to ensure a *str does not grow indefinitely.
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

void vec_remove(struct vec *v, size_t n) {
  assert(n < v->length);
  assert(v->length);
  void *dst = v->content + n * v->element_size;
  const void *src = v->content + (n + 1) * v->element_size;
  size_t count = (v->length - n - 1) * v->element_size;
  memmove(dst, src, count);
  v->length--;
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
  v->length = v->capacity = 0;
  free(v->content);
  v->content = nullptr;
}

void *vec_new_element(struct vec *v) {
  vec_push(v, nullptr);
  return vec_nth(*v, v->length - 1);
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

#define HASHMAP_MAX_LOAD (0.7)

[[gnu::const]] static uint32_t hashmap_hash(uint32_t key) {
  return key * 7;
}
static uint64_t hashmap_next_size(struct hashmap *h) {
  if (h->capacity <= 1) return 13;
  return h->capacity * 2 - 1;
}

// Find key, probing past tombstones.
static bool hashmap_find_key(const struct hashmap *h, uint32_t key, uint32_t *index) {
  for (uint32_t i = 0; i < h->capacity; i++) {
    uint32_t slot = (i + hashmap_hash(key)) % h->capacity;
    enum hashmap_slot_state s = h->metadata[slot];
    switch (s) {
    case HASHMAP_SLOT_PRISTINE: {
      return false;
    }
    case HASHMAP_SLOT_OCCUPIED: {
      if (h->keys[slot] == key) {
        *index = slot;
        return true;
      }
      // keep probing
    } break;
    case HASHMAP_SLOT_TOMBSTONE: continue; // keep probing
    }
  }
  return false;
}

static void hashmap_maybe_enlarge(struct hashmap *h);

// Only call this *after* ensuring that the key is not present.
// This should functionaly be the *only* way to increase the count of the map
static void hashmap_add_unchecked(struct hashmap *h, uint32_t key, void *value) {
  for (uint32_t i = 0; i < h->capacity; i++) {
    uint32_t slot = (i + hashmap_hash(key)) % h->capacity;
    enum hashmap_slot_state s = h->metadata[slot];
    if (s == HASHMAP_SLOT_OCCUPIED) continue;
    h->count++;
    h->keys[slot] = key;
    h->values[slot] = value;
    h->metadata[slot] = HASHMAP_SLOT_OCCUPIED;
    return;
  }
  assert(!"Unreachable");
}

static void hashmap_maybe_enlarge(struct hashmap *h) {
  float item_load = (float)(h->count + 1) / (float)h->capacity;
  if (item_load > HASHMAP_MAX_LOAD || h->capacity == 0) {
    uint32_t cap = hashmap_next_size(h);
    size_t v_size = sizeof(h->values);
    size_t k_size = sizeof(h->keys);
    size_t m_size = sizeof(h->metadata);
    uint8_t *data = calloc(cap, v_size + k_size + m_size);
    void *values = data;
    void *keys = data + cap * v_size;
    void *metadata = data + cap * v_size + cap * k_size;
    struct hashmap new = {.values = values, .keys = keys, .metadata = metadata, .capacity = cap};
    for (uint32_t i = 0; i < h->capacity; i++) {
      if (h->metadata[i] == HASHMAP_SLOT_OCCUPIED) {
        hashmap_add(&new, h->keys[i], h->values[i]);
      }
    }
    hashmap_destroy(h);
    *h = new;
  }
}

bool hashmap_add(struct hashmap *h, uint32_t key, void *value) {
  uint32_t slot;
  if (hashmap_find_key(h, key, &slot)) {
    return false;
  } else {
    hashmap_maybe_enlarge(h);
    hashmap_add_unchecked(h, key, value);
    return true;
  }
  assert(!"Unreachable");
}

void *hashmap_set(struct hashmap *h, uint32_t key, void *value) {
  uint32_t slot;
  if (hashmap_find_key(h, key, &slot)) {
    void *removed = h->values[slot];
    h->values[slot] = value;
    return removed;
  } else {
    hashmap_maybe_enlarge(h);
    hashmap_add_unchecked(h, key, value);
    return nullptr;
  }
  assert(!"Unreachable");
}

void *hashmap_get(const struct hashmap *h, uint32_t key) {
  uint32_t slot;
  if (hashmap_find_key(h, key, &slot)) {
    return h->values[slot];
  }
  return nullptr;
}

bool hashmap_contains(const struct hashmap *h, uint32_t key) {
  uint32_t slot;
  return hashmap_find_key(h, key, &slot);
}

static uint32_t hashmap_next_slot(struct hashmap *h, uint32_t slot) {
  return (slot + 1 + h->capacity) % h->capacity;
}
static uint32_t hashmap_prev_slot(struct hashmap *h, uint32_t slot) {
  return (slot - 1 + h->capacity) % h->capacity;
}

bool hashmap_remove(struct hashmap *h, uint32_t key, void **removed) {
  uint32_t slot;
  if (hashmap_find_key(h, key, &slot)) {
    if (removed) *removed = h->values[slot];
    // Mark this slot as a tombstone to allow for probing in case of collisions
    h->metadata[slot] = HASHMAP_SLOT_TOMBSTONE;
    // If the next slot is pristine, this slot cannot be used for probing, and must also be pristine
    while (h->metadata[hashmap_next_slot(h, slot)] == HASHMAP_SLOT_PRISTINE &&
           h->metadata[slot] == HASHMAP_SLOT_TOMBSTONE) {
      h->metadata[slot] = HASHMAP_SLOT_PRISTINE;
      slot = hashmap_prev_slot(h, slot);
    }
    h->count--;
    return true;
  }
  return false;
#undef next
#undef prev
}

void vec_swap(struct vec *v, size_t i, size_t j) {
  assert(i >= 0 && i < v->length);
  assert(j >= 0 && j < v->length);
  void *tmp = vec_new_element(v);
  v->length--;
  void *x = vec_nth(*v, i);
  void *y = vec_nth(*v, j); 
  memcpy(tmp, x, v->element_size);
  memcpy(x, y, v->element_size);
  memcpy(y, tmp, v->element_size);
}

void hashmap_destroy(struct hashmap *h) {
  free(h->values);
}
