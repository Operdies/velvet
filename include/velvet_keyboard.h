#ifndef VELVET_KEYBOARD_H
#define VELVET_KEYBOARD_H

#include <stdint.h>
#include <velvet_api.h>

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

struct velvet_key_event {
  struct velvet_key key;
  enum velvet_api_key_modifiers modifiers;
  enum velvet_api_key_event_type type;
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
