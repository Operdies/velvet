#ifndef VELVET_INPUT_H
#define VELVET_INPUT_H

#include "collections.h"
#include "multiplexer.h"

enum velvet_input_state {
  VELVET_INPUT_STATE_NORMAL,
  VELVET_INPUT_STATE_ESC,
  VELVET_INPUT_STATE_CSI,
  VELVET_INPUT_STATE_PREFIX,
  VELVET_INPUT_STATE_PREFIX_CONT,
  VELVET_INPUT_STATE_BRACKETED_PASTE,
};

struct velvet_input {
  struct multiplexer *m;
  uint8_t prefix;
  enum velvet_input_state state;
  struct string command_buffer;
};


void velvet_input_process(struct velvet_input *in, struct u8_slice str);
void velvet_input_destroy(struct velvet_input *in);

#endif // VELVET_INPUT_H
