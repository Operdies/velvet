#ifndef VELVET_KEYBOARD_H
#define VELVET_KEYBOARD_H

#include <stdint.h>
#include <string.h>

/* masks matching the Kitty keyboard protocol spec: https://sw.kovidgoyal.net/kitty/keyboard-protocol/ */
enum velvet_key_modifier {
  MODIFIER_SHIFT     = 0b1,        //(1)
  MODIFIER_ALT       = 0b10,       //(2)
  MODIFIER_CTRL      = 0b100,      //(4)
  MODIFIER_SUPER     = 0b1000,     //(8)
  MODIFIER_HYPER     = 0b10000,    //(16)
  MODIFIER_META      = 0b100000,   //(32)
  MODIFIER_CAPS_LOCK = 0b1000000,  //(64)
  MODIFIER_NUM_LOCK  = 0b10000000, //(128)
};

static char VELVET_SHIFT_TABLE[] = {
    ['!'] = '1',  ['@'] = '2', ['#'] = '3', ['$'] = '4', ['%'] = '5', ['^'] = '6', ['&'] = '7',
    ['*'] = '8',  ['('] = '9', [')'] = '0', ['<'] = ',', ['>'] = '.', [':'] = ';', ['"'] = '\'',
    ['|'] = '\\', ['~'] = '`', ['?'] = '/', ['{'] = '[', ['}'] = ']',
};


struct velvet_key {
  char *name;
  char *escape;
  uint32_t codepoint;
  /* if a modifier such as shift has altered the symbol,
   * the original symbol is available in codepoint, and the altered
   * symbol is available in alternate_codepoint */
  uint32_t alternate_codepoint;
  char kitty_terminator;
};

enum velvet_key_event_type {
  KEY_PRESS   = 1,
  KEY_REPEAT  = 2,
  KEY_RELEASE = 3,
};

struct velvet_key_event {
  struct velvet_key key;
  enum velvet_key_modifier modifiers;
  enum velvet_key_event_type type;
  struct {
    /* TODO: How long can this be ? */
    uint32_t codepoints[2];
    int n;
  } associated_text;
  /* set to true if the key was not obtained through a kitty escape sequence.
   * In this case, we should send it as a basic key.*/
  bool legacy;
  /* set if the mapping was removed. If removed is set, no other fields will be set. */
  bool removed;
};

#define X(n, e, cp, f) {.name = n, .escape = e, .codepoint = cp, .kitty_terminator = f},
static const struct velvet_key named_keys[] = {
    {.name = "SPACE", .codepoint = ' ', .kitty_terminator = 'u', .escape = " "},
#include "kitty_keys.def"
};
#undef X

#endif
