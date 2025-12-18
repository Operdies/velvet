#include "collections.h"
#include "utils.h"
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

static size_t next_size(size_t min) {
  size_t next = 4;
  while (next < min) next = next * 2;
  return next;
}

static void string_ensure_capacity(struct string *str, size_t required) {
  if (str->content == nullptr || str->cap < required) {
    uint8_t *prev = str->content;
    size_t newsize = next_size(required);
    str->content = velvet_calloc(newsize, 1);
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
  size_t idx = max;

  do {
    buf[--idx] = (uint8_t)('0' + n % 10);
    n /= 10;
  } while (n);

  string_push_range(str, buf + idx, max - idx);
}

void string_push_csi(struct string *str, uint8_t leading, struct int_slice params, const char *const final) {
  const uint8_t *csi = u8"\x1b[";
  string_push(str, csi);
  if (leading) string_push_char(str, leading);
  for (size_t i = 0; i < params.n; i++) {
    assert(params.content[i] >= 0);
    if (i) string_push_char(str, ';');
    string_push_int(str, params.content[i]);
  }
  string_push(str, (uint8_t *)final);
}

void string_push_cstr(struct string *str, char *cstr) {
  string_push_slice(str, u8_slice_from_cstr(cstr));
}

void string_push_slice(struct string *str, struct u8_slice slice) {
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

bool u8_slice_starts_with_cstr(struct u8_slice slice, char *str) {
  size_t i = 0;
  for (; *str && i < slice.len; i++, str++)
    if (slice.content[i] != *str) return false;
  return true;
}

bool u8_slice_starts_with(struct u8_slice slice, struct u8_slice prefix) {
  if (!slice.content || !prefix.content) return false;
  if (slice.len < prefix.len) return false;
  if (slice.content == prefix.content) return true;
  return memcmp(slice.content, prefix.content, prefix.len) == 0;
}

bool string_starts_with(struct string *str, struct u8_slice slice) {
  return u8_slice_starts_with(string_as_u8_slice(str), slice);
}

bool string_ends_with(struct string *str, struct u8_slice slice) {
  if (!str->content || !slice.content) return false;
  if (str->len < slice.len) return false;
  return memcmp(str->content + str->len - slice.len, slice.content, slice.len) == 0;
}

void string_drop_left(struct string *str, size_t n) {
  assert(n <= str->len);
  size_t copy = str->len - n;
  if (n && copy) {
    memmove(str->content, str->content + n, copy);
  }
  str->len -= n;
}


void string_memset(struct string *str, uint8_t ch, size_t len) {
  // Some big number. This would most likely be caused by an overflow, and not some legitimate allocation.
  assert(len < (1 << 30));
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

void vec_ensure_capacity(struct vec *v, size_t c) {
  assert(v->element_size && "Element size cannot be 0");
  if (v->capacity >= c) return;
  // For the initial allocation, try to allocate at least one page. Failing that, just allocate a couple of elements.
  size_t min_capacity = MAX(4, 4096 / (int)v->element_size);
  if (v->capacity <= 0) 
    v->capacity = min_capacity;
  while (v->capacity < c) {
    v->capacity *= 2;
    if (v->capacity <= 0) {
      velvet_die("Extremely large vector: 0x%zx", c);
      v->capacity = c;
      break;
    }
  }
  if (v->content) {
    v->content = velvet_erealloc(v->content, v->capacity, v->element_size);
  } else {
    v->content = velvet_calloc(v->capacity, v->element_size);
  }
}

ssize_t vec_index(struct vec *v, const void *const item) {
  if (!v->length || !item) return -1;
  char *it = (char *)item;
  char *base = v->content;
  char *end = base + (v->element_size * v->length);
  if (it < base) return -1;
  if (it >= end) return -1;
  size_t offset = it - base;
  assert(offset % v->element_size == 0);
  return offset / v->element_size;
}

void vec_swap_remove(struct vec *v, void *e) {
  ssize_t index = vec_index(v, e);
  assert(index >= 0);
  assert(v->length > 0);
  v->length = v->length - 1;
  if (v->length) {
    void *last = vec_nth(v, v->length);
    memcpy(e, last, v->element_size);
  }
}

void *vec_pop(struct vec *v) {
  if (v->length == 0) return nullptr;
  v->length--;
  return vec_nth(v, v->length);
}

void vec_remove(struct vec *v, void *e) {
  ssize_t index = vec_index(v, e);
  assert(index >= 0);
  vec_remove_at(v, index);
}

void vec_remove_at(struct vec *v, size_t n) {
  assert(n < v->length);
  assert(v->length);
  void *dst = v->content + n * v->element_size;
  const void *src = v->content + (n + 1) * v->element_size;
  size_t count = (v->length - n - 1) * v->element_size;
  memmove(dst, src, count);
  v->length--;
}

void vec_set(struct vec *v, size_t i, const void *elem) {
  size_t initial_length = v->length;
  v->length = MAX(i + 1, v->length);
  vec_ensure_capacity(v, v->length);
  void *item = vec_nth(v, i);
  if (elem)
    memcpy(item, elem, v->element_size);
  else
    memset(item, 0, v->element_size);

  void *start = vec_nth(v, initial_length);
  void *end = (char*)v->content + (i * v->element_size);
  if (start < end) {
    memset(start, 0, end - start);
  }
}

void vec_push(struct vec *v, const void *elem) {
  assert(v->element_size && "Element size cannot be 0");
  v->length++;
  vec_ensure_capacity(v, v->length);
  void *last = vec_nth(v, v->length - 1);
  if (elem)
    memcpy(last, elem, v->element_size);
  else
    memset(last, 0, v->element_size);
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
  return vec_nth(v, v->length - 1);
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
}

void *hashmap_set(struct hashmap *h, uint32_t key, void *value) {
  uint32_t slot;
  void *ret = nullptr;
  if (hashmap_find_key(h, key, &slot)) {
    void *removed = h->values[slot];
    h->values[slot] = value;
    ret = removed;
  } else {
    hashmap_maybe_enlarge(h);
    hashmap_add_unchecked(h, key, value);
  }
  return ret;
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

struct u8_slice u8_slice_from_cstr(const char *const str) {
  return (struct u8_slice) { .len = strlen(str), .content = (uint8_t*)str };
}

struct u8_slice string_as_u8_slice(const struct string *const s) {
  return (struct u8_slice) { .content = s->content, .len = s->len };
}

bool u8_slice_equals(struct u8_slice a, struct u8_slice b) {
  if (a.len != b.len) return false;
  if (a.content == b.content) return true;
  return memcmp(a.content, b.content, a.len) == 0;
}

bool u8_slice_contains(struct u8_slice s, uint8_t ch) {
  for (size_t i = 0; i < s.len; i++) if (s.content[i] == ch) return true;
  return false;
}

struct u8_slice u8_slice_strip(struct u8_slice s, struct u8_slice chars) {
  size_t start, end;
  for (start = 0; start < s.len && u8_slice_contains(chars, s.content[start]);) start++;
  for (end = s.len - 1; end > start && u8_slice_contains(chars, s.content[end]);) end--;
  return u8_slice_range(s, start, end + 1);
}

struct u8_slice u8_slice_strip_quotes(struct u8_slice s) {
  char quotes[] = {'\'', '"'};
  for (int i = 0; i < LENGTH(quotes); i++) {
    if (s.len >= 2 && s.content[0] == quotes[i] && s.content[s.len - 1] == quotes[i]) {
      return u8_slice_range(s, 1, -2);
    }
  }
  return s;
}

struct u8_slice u8_slice_strip_whitespace(struct u8_slice s) {
  return u8_slice_strip(s, u8_slice_from_cstr(" \v\f\t\r\n"));
}

struct u8_slice u8_slice_range(struct u8_slice s, ssize_t start, ssize_t end) {
  if (end < 0) {
    end = s.len + end + 1;
  }
  if (start < 0) {
    start = s.len + start + 1;
  }
  size_t length = end - start;
  assert(start >= 0);
  assert(end <= (ssize_t)s.len);
  assert(start + length <= s.len);
  struct u8_slice slice = { .content = s.content + start, .len = length };
  return slice;
}

/* whole string: string_range(s, 0, s->len)
 * Also while string: string_range(s, 0, -1)
 * Strip first and last: string_range(s, 1, -2)
 * Last 10: string_range(s, -11, -1)
 * */
struct u8_slice string_range(const struct string *const s, ssize_t start, ssize_t end) {
  struct u8_slice s2 = string_as_u8_slice(s);
  return u8_slice_range(s2, start, end);
}

void vec_swap(struct vec *v, size_t i, size_t j) {
  assert(i < v->length);
  assert(j < v->length);
  void *tmp = vec_new_element(v);
  v->length--;
  void *x = vec_nth(v, i);
  void *y = vec_nth(v, j); 
  memcpy(tmp, x, v->element_size);
  memcpy(x, y, v->element_size);
  memcpy(y, tmp, v->element_size);
}

void *vec_nth(const struct vec *const v, size_t i) {
  assert(i < v->length);
  const char *const base = v->content;
  size_t offset = i * v->element_size;
  return (void*)base + offset;
}

void hashmap_destroy(struct hashmap *h) {
  free(h->values);
}

void string_push_format_slow(struct string *s, char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  size_t required = vsnprintf(nullptr, 0, fmt, ap);
  va_end(ap);

  string_ensure_capacity(s, s->len + required + 1);
  va_start(ap, fmt);
  s->len += vsnprintf((char*)s->content + s->len, s->cap - s->len, fmt, ap);
  va_end(ap);
}

