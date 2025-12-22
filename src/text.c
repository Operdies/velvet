#include "text.h"
#include "utils.h"
#include <string.h>
#include <wchar.h>
#include <string.h>

// assume *str is a valid utf8 string
int utf8_strlen(char *str) {
  int i = 0;
  for (; *str; str++)
    if (((uint8_t)(*str) & 0xC0) != 0x80) i++;
  return i;
}

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

struct codepoint utf8_to_codepoint(const uint8_t utf8[4], int *len) {
  struct codepoint cp = { 0 };

  if (utf8[0] < 0x80) {
    cp.value = utf8[0];
    *len = 1;
  } else if ((utf8[0] & 0xE0) == 0xC0) {
    cp.value = ((utf8[0] & 0x1F) << 6) | (utf8[1] & 0x3F);
    *len = 2;
  } else if ((utf8[0] & 0xF0) == 0xE0) {
    cp.value = ((utf8[0] & 0x0F) << 12) | ((utf8[1] & 0x3F) << 6) | (utf8[2] & 0x3F);
    *len = 3;
  } else if ((utf8[0] & 0xF8) == 0xF0) {
    cp.value = ((utf8[0] & 0x07) << 18) | ((utf8[1] & 0x3F) << 12) | ((utf8[2] & 0x3F) << 6) | (utf8[3] & 0x3F);
    *len = 4;
  } else {
    cp.value = 0xFFFD; // replacement character
    *len = 1;
  }

  cp.is_wide = wcwidth(cp.value) > 1;
  return cp;
}

int codepoint_to_utf8(uint32_t cp, struct utf8 *u) {
  if (cp <= 0x7F) {
    u->utf8[0] = cp;
    return 1;
  } else if (cp <= 0x7FF) {
    u->utf8[0] = 0xC0 | (cp >> 6);
    u->utf8[1] = 0x80 | (cp & 0x3F);
    return 2;
  } else if (cp <= 0xFFFF) {
    u->utf8[0] = 0xE0 | (cp >> 12);
    u->utf8[1] = 0x80 | ((cp >> 6) & 0x3F);
    u->utf8[2] = 0x80 | (cp & 0x3F);
    return 3;
  } else if (cp <= 0x10FFFF) {
    u->utf8[0] = 0xF0 | (cp >> 18);
    u->utf8[1] = 0x80 | ((cp >> 12) & 0x3F);
    u->utf8[2] = 0x80 | ((cp >> 6) & 0x3F);
    u->utf8[3] = 0x80 | (cp & 0x3F);
    return 4;
  } else {
    // Invalid Unicode scalar value → U+FFFD
    u->utf8[0] = 0xEF;
    u->utf8[1] = 0xBF;
    u->utf8[2] = 0xBD;
    return 3;
  }
}

void utf8_push(struct utf8 *u, uint8_t byte) {
  assert(u->utf8[3] == 0);
  uint8_t n = utf8_length(*u);
  u->utf8[n] = byte;
}
