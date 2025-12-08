#ifndef TEXT_H
#define TEXT_H

#include <stdint.h>

struct utf8 {
  union {
    /* byte sequence */
    uint8_t utf8[4];
    /* Used for equality -- NOT a unicode codepoint */
    uint32_t numeric;
  };
};

static const struct utf8 utf8_fffd = {.utf8 = {0xEF, 0xBF, 0xBD}};
static const struct utf8 utf8_blank = {.utf8 = {' '}};
static const struct utf8 utf8_zero = {0};

int utf8_strlen(char *str);
uint8_t utf8_expected_length(uint8_t ch);
uint8_t utf8_length(struct utf8 u);
void utf8_push(struct utf8 *u, uint8_t byte);
bool utf8_equals(struct utf8 a, struct utf8 b);

#endif /*  TEXT_H */
