#ifndef VELVET_KEYBOARD_H
#define VELVET_KEYBOARD_H

#include <string.h>

struct special_key {
  char *name;
  char *escape;
};

#define SPECIAL_KEYS                                                                                                   \
  X(F1, "\x1bOP")                                                                                                      \
  X(F2, "\x1bOQ")                                                                                                      \
  X(F3, "\x1bOR")                                                                                                      \
  X(F4, "\x1bOS")                                                                                                      \
  X(F5, "\x1b[15~")                                                                                                    \
  X(F6, "\x1b[17~")                                                                                                    \
  X(F7, "\x1b[18~")                                                                                                    \
  X(F8, "\x1b[19~")                                                                                                    \
  X(F9, "\x1b[20~")                                                                                                    \
  X(F10, "\x1b[21~")                                                                                                   \
  X(F11, "\x1b[23~")                                                                                                   \
  X(F12, "\x1b[24~")                                                                                                   \
  X(SS3_UP, "\x1bOA")                                                                                                  \
  X(UP, "\x1b[A")                                                                                                      \
  X(SS3_DOWN, "\x1bOB")                                                                                                \
  X(DOWN, "\x1b[B")                                                                                                    \
  X(SS3_RIGHT, "\x1bOC")                                                                                               \
  X(RIGHT, "\x1b[C")                                                                                                   \
  X(SS3_LEFT, "\x1bOD")                                                                                                \
  X(LEFT, "\x1b[D")                                                                                                    \
  X(ESC, "\x1b")                                                                                                       \
  X(DEL, "\x1b[3~")                                                                                                    \
  X(BS, "\x08")                                                                                                        \
  X(BS, "\x7f")                                                                                                        \
  X(SPACE, " ")

static const struct special_key keys[] = {
#define X(x, y) {.name = #x, .escape = y},
    SPECIAL_KEYS
#undef X
};

struct velvet_key {
  bool literal;
  union {
    char symbol;
    struct special_key special;
  };
};

#endif
