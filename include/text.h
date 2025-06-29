#ifndef TEXT_H
#define TEXT_H

#include <stdint.h>

struct utf8 {
  /* the current byte sequence */
  uint8_t utf8[8];
  /* the length of the current sequence */
  uint8_t len;
  /* the expected total length based on the leading byte */
  uint8_t expected;
  /* TODO: the width of this character */
  uint8_t width;
};

static const struct utf8 utf8_fffd = {.len = 3, .utf8 = {0xEF, 0xBF, 0xBD}};
static const struct utf8 utf8_blank = {.len = 1, .utf8 = {' '}};

void utf8_push(struct utf8 *u, uint8_t byte);
bool utf8_equals(const struct utf8 *const a, const struct utf8 *const b);

#endif /*  TEXT_H */
