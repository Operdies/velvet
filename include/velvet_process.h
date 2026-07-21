#ifndef VELVET_PROCESS_H
#define VELVET_PROCESS_H

#include "velvet.h"

struct velvet_process_stream_options {
  bool in, out, err;
};

struct velvet_process {
  int id, pid;
  struct string pending_input;
  int in, out, err;
  int exit_code;
  int term_signal;
  /* handles to lua functions */
  struct {
    /* fun(data: string): nil */
    int on_stdout;
    /* fun(data: string): nil */
    int on_stderr;
    /* fun(exit_code: integer): nil */
    int on_exit;
  } callbacks;
  bool stdin_closed;
  bool killed;
  /* set after attempting to terminate. If this grace period expires, it will be killed rudely. */
  uint64_t termination_deadline;
};


int velvet_process_spawn(struct velvet *v, char *wd, char **argv, char **envp, struct velvet_process_stream_options streams);
void velvet_process_write_stdin(struct velvet *v, struct velvet_process *p, struct u8_slice text);
void velvet_process_close_stdin(struct velvet *v, struct velvet_process *p);
void velvet_process_kill_and_destroy_all(struct velvet *v);
void velvet_process_kill(struct velvet *v, struct velvet_process *p, int signal);
void velvet_process_destroy(struct velvet_process *p);

/* Not defined in the implementation file. Instead, they are implemented in velvet_api.c
 * This is a bit strange but just feels more right because the implementations are all lua vm manipulation.
 * The alternative would be to use function pointers in the struct definition but I don't want to. */
void velvet_process_on_exit(struct velvet *v, struct velvet_process *p);
void velvet_process_on_stdout(struct velvet *v, struct velvet_process *p, struct u8_slice data);
void velvet_process_on_stderr(struct velvet *v, struct velvet_process *p, struct u8_slice data);

#endif
