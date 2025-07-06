#include "text.h"
#include "utils.h"
#include <string.h>

uint8_t utf8_expected_length(uint8_t ch) {
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

uint8_t utf8_length(struct utf8 u) {
  if (u.utf8[3])
    return 4;
  if (u.utf8[2])
    return 3;
  if (u.utf8[1])
    return 2;
  if (u.utf8[0])
    return 1;
  return 0;
}

void utf8_push(struct utf8 *u, uint8_t byte) {
  assert(u->utf8[3] == 0);
  uint8_t n = utf8_length(*u);
  u->utf8[n] = byte;
}

bool utf8_equals(const struct utf8 *const a, const struct utf8 *const b) {
  return a->numeric == b->numeric;
}
