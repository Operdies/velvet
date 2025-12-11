#ifndef VELVET_H
#define VELVET_H

#include "collections.h"
#include "io.h"
#include "platform.h"
#include "velvet_scene.h"
#include "velvet_input.h"

struct velvet_session {
  int socket;                   // socket connection
  struct string pending_output; // buffered output
  int input;                    // stdin
  int output;                   // stdout
  struct platform_winsize ws;
};

struct velvet {
  struct velvet_scene scene;
  struct velvet_input input_handler;
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

#endif
