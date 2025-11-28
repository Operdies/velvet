#ifndef VELVET_INPUT_H
#define VELVET_INPUT_H

#include "collections.h"
#include "multiplexer.h"

struct velvet_input {
  struct multiplexer *m;
  int state;
};


void velvet_input_process(struct velvet_input *in, struct u8_slice str);

#endif // VELVET_INPUT_H
