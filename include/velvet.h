#ifndef VELVET_H
#define VELVET_H

#include "collections.h"
#include "io.h"
#include "platform.h"
#include "velvet_scene.h"

enum velvet_input_state {
  VELVET_INPUT_STATE_NORMAL,
  VELVET_INPUT_STATE_ESC,
  VELVET_INPUT_STATE_CSI,
  VELVET_INPUT_STATE_PREFIX,
  VELVET_INPUT_STATE_PREFIX_CONT,
  VELVET_INPUT_STATE_BRACKETED_PASTE,
};

struct velvet_input_options {
  bool focus_follows_mouse;
};

struct velvet_input {
  uint8_t prefix;
  enum velvet_input_state state;
  struct string command_buffer;
  struct velvet_input_options options;
};


struct velvet_session {
  int socket;                   // socket connection
  struct string pending_output; // buffered output
  int input;                    // stdin
  int output;                   // stdout
  struct platform_winsize ws;
};

struct velvet {
  struct velvet_scene scene;
  struct velvet_input input;
  struct io event_loop;
  struct vec /* struct session */ sessions;
  /* this is modified by events such as receiving focus IN/OUT events, new sessions attaching, etc */
  size_t active_session;
  int socket;
  int signal_read;
  bool quit;
};

void velvet_loop(struct velvet *velvet);
void velvet_destroy(struct velvet *velvet);
void velvet_process_input(struct velvet *in, struct u8_slice str);
void velvet_input_destroy(struct velvet_input *v);

#endif
