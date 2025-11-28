#include "velvet_input.h"

void velvet_input_process(struct velvet_input *in, struct u8_slice str) {
  multiplexer_feed_input(in->m, str);
}
