#ifndef TEXT_H
#define TEXT_H

#include <stdint.h>

struct utf8 {
  uint8_t utf8[4];
};

struct unicode_codepoint {
  uint32_t cp;
  bool wide;
};

constexpr struct unicode_codepoint codepoint_fffd = { .cp = 0xFFFD };
constexpr struct unicode_codepoint codepoint_space = { .cp = ' ' };
constexpr struct unicode_codepoint codepoint_zero = { .cp = 0 };

constexpr struct utf8 utf8_fffd = {.utf8 = {0xEF, 0xBF, 0xBD}};
constexpr struct utf8 utf8_blank = {.utf8 = {' '}};
constexpr struct utf8 utf8_zero = {0};


int utf8_strlen(char *str);
uint8_t utf8_expected_length(uint8_t ch);
uint8_t utf8_length(struct utf8 u);
void utf8_push(struct utf8 *u, uint8_t byte);
struct unicode_codepoint utf8_to_codepoint(const uint8_t utf8[4], int *len);
int codepoint_to_utf8(uint32_t cp, struct utf8 *u);

#endif /*  TEXT_H */
