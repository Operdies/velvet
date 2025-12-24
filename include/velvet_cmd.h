#ifndef VELVET_CMD_H
#define VELVET_CMD_H

#include "collections.h"
#include "velvet.h"

struct velvet_cmd_iterator {
  struct u8_slice src;
  size_t cursor;
  int line_count;
  struct u8_slice current;
  bool require_terminator;
};

struct velvet_cmd_arg_iterator {
  struct u8_slice src;
  size_t cursor;
  struct u8_slice current;
};

bool velvet_cmd_iterator_next(struct velvet_cmd_iterator *s);
bool velvet_cmd_arg_iterator_next(struct velvet_cmd_arg_iterator *s);
bool velvet_cmd_arg_iterator_unget(struct velvet_cmd_arg_iterator *it);
void velvet_cmd(struct velvet *v, int source_socket, struct u8_slice cmd);

#endif
