#include "text.h"
#include "utils.h"
#include <string.h>

static inline uint8_t utf8_expected_length(uint8_t ch) {
  if ((ch & 0x80) == 0x00)
    return 1; /* 0xxxxxxx */
  else if ((ch & 0xE0) == 0xC0)
    return 2; /* 110xxxxx */
  else if ((ch & 0xF0) == 0xE0)
    return 3; /* 1110xxxx */
  else if ((ch & 0xF8) == 0xF0)
    return 4; /* 11110xxx */
  else
    return 0; /* invalid leading byte or continuation byte */
}

void utf8_push(struct utf8 *u, uint8_t byte) {
  assert(u->len < 8);
  if (!u->len) {
    uint8_t expected_length = utf8_expected_length(byte);
    u->expected = expected_length;
  }
  u->utf8[u->len] = byte;
  u->len++;
}

bool utf8_equals(const struct utf8 *const a, const struct utf8 *const b) {
  return a->len == b->len && memcmp(a->utf8, b->utf8, a->len) == 0;
}
