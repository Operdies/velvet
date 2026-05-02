#ifndef VELVET_PROCESS_H
#define VELVET_PROCESS_H

#include "velvet.h"

struct velvet_process {
  int id, pid;
  struct string pending_input;
  int in, out, err;
  int exit_code;
  bool stdin_closed;
  bool destroy_pending;
};


int velvet_process_spawn(struct velvet *v, char *wd, char **argv, char **envp);
void velvet_process_close_stdin(struct velvet *v, struct velvet_process *p);
void velvet_process_kill(struct velvet *v, struct velvet_process *p);
void velvet_process_destroy(struct velvet *v, struct velvet_process *p);

#endif
