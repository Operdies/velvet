#ifndef TEXT_H
#define TEXT_H

#include <stdint.h>

struct utf8 {
  uint8_t utf8[4];
};

struct codepoint {
  uint32_t value;
  bool is_wide;
};

constexpr struct codepoint codepoint_fffd = { .value = 0xFFFD };
constexpr struct codepoint codepoint_space = { .value = ' ' };
constexpr struct codepoint codepoint_zero = { .value = 0 };

constexpr struct utf8 utf8_fffd = {.utf8 = {0xEF, 0xBF, 0xBD}};
constexpr struct utf8 utf8_blank = {.utf8 = {' '}};
constexpr struct utf8 utf8_zero = {0};


int utf8_strlen(char *str);
uint8_t utf8_expected_length(uint8_t ch);
uint8_t utf8_length(struct utf8 u);
void utf8_push(struct utf8 *u, uint8_t byte);
struct codepoint utf8_to_codepoint(const uint8_t utf8[4], int *len);
int codepoint_to_utf8(uint32_t cp, struct utf8 *u);

#endif /*  TEXT_H */
