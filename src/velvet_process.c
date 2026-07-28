#include "platform.h"
#include "velvet.h"
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>
#include "collections.h"
#include "velvet_process.h"
#include <fcntl.h>

static int nullfd = -2;

static void restore_signals(void) {
  /* restore default handlers for a couple of terminating signals.
   * This is needed because their signal handlers would otherwise
   * deliver signals to the parent process via a pipe until exec() is called.
   * Now, if these signals are delivered before exec(), the child process should
   * hopefully be reaped by the parent instead.
   * */
  int restore[] = {SIGTERM, SIGINT, SIGHUP};
  struct sigaction sa = {0};
  sa.sa_handler = SIG_DFL;
  for (int i = 0; i < LENGTH(restore); i++) sigaction(restore[i], &sa, NULL);
}

static char *find_binary(const char *name, const char *p) {
  static char temp[PATH_MAX] = {0};
  const char *p_end = p + strlen(p);
  int n_len = strlen(name);
  while (p < p_end) {
    const char *next = strchr(p, ':');
    if (!next) next = p_end;
    int i = next - p;
    if (i + n_len + 2 >= PATH_MAX) {
      /* path would be too long, go next */
      p = next + 1;
      continue;
    }

    memcpy(temp, p, i);
    if (temp[i-1] != '/') {
      temp[i++] = '/';
    }
    memcpy(temp + i, name, n_len);
    i += n_len;
    temp[i] = 0;

    int mask = F_OK | R_OK | X_OK;
    if (access(temp, mask) == 0) {
      return temp;
    }

    p = next + 1;
  }
  return NULL;
}

_Noreturn static void process_setup_child(int error_pipe, const char *wd,
                                          const char *filename,
                                          char *const *argv, char *const *envp,
                                          int in, int out, int err) {
  if (!wd) {
    /* the working directory of the current process is likely to be
     * something like /usr/local/share/velvet or similar.
     * Most processes would probably prefer to be running in the home folder or something. */
    wd = getenv("HOME");
  }
  if (chdir(wd) == -1) {
    int err = errno;
    write(error_pipe, &err, sizeof(int));
    exit(0);
  }

  if (in == nullfd || out == nullfd || err == nullfd) {
    int devnull = open("/dev/null", O_RDWR);
    if (in == nullfd) in = devnull;
    if (out == nullfd) out = devnull;
    if (err == nullfd) err = devnull;
  }

  dup2(in, STDIN_FILENO);
  dup2(out, STDOUT_FILENO);
  dup2(err, STDERR_FILENO);

  close(in);
  if (out != in) close(out);
  if (err != out && err != in) close(err);
  /* close read side in fork */

  if (envp) {
    execve(filename, argv, envp);
  } else {
    execv(filename, argv);
  }
  int exec_error = errno;
  write(error_pipe, &exec_error, sizeof(int));
  exit(0);
  /* write side automatically cleaned up in child due to cloexec */
}

static int pipe_pair_create_cloexec(int *r, int *w) {
  int fds[2];
  if (pipe(fds) < 0) return errno;
  set_cloexec(fds[0]);
  set_cloexec(fds[1]);
  *r = fds[0];
  *w = fds[1];
  return 0;
}

static char *find_path(char **envp) {
  static char *default_path = NULL;
  /* 1. if envp specifies PATH, use that.
   * Otherwise use the system path. */
  for (; envp && *envp; envp++) {
    char *env = *envp;
    if (strncmp("PATH=", env, 5) == 0)
      return (*envp) + 5;
  }

  /* 2. check if PATH is defined */
  char *path = getenv("PATH");
  if (path) 
    return path;

  /* 3. fall back to the systems default PATH */
  if (!default_path) {
    /* lazily initialize default_path and reuse it for the program's lifetime */
    default_path = "";
    size_t len = confstr(_CS_PATH, NULL, 0);
    if (len > 0) {
      default_path = velvet_calloc(len, sizeof(char));
      confstr(_CS_PATH, default_path, len);
    }
  }
  return default_path;
}

struct streams {
  int in, out, err;
};

static int spawn_process(struct velvet_process *p, const char *filename, char *wd, char **argv, char **envp, struct streams streams) {
  assert(argv && argv[0] && argv[0][0]);

  int guard_read = 0;
  int guard_write = 0;
  int status = pipe_pair_create_cloexec(&guard_read, &guard_write);
  if (status != 0) { 
    return status;
  }

  /* fork sequence largely copied from velvet_scene.c. The same principles apply;
  * block signal generation before forking so the child cannot write to velvet's signal pipes. */
  sigset_t block, sighandler, trash_signalset;
  sigfillset(&block);
  sigprocmask(SIG_BLOCK, &block, &sighandler);

  pid_t pid = fork();

  if (pid == 0) {
    restore_signals();
  }

  sigprocmask(SIG_SETMASK, &sighandler, &trash_signalset);

  if (pid < 0) {
    close(guard_read); close(guard_write);
    ERROR("Unable to spawn process:");
    return errno;
  }

  if (pid == 0) {
    /* close read side in child */
    close(guard_read);
    process_setup_child(guard_write, wd, filename, argv, envp, streams.in, streams.out, streams.err);
    /* child does not return here */
  }

  /* Close write side in parent. Otherwise read(rw[0]) will block. */
  close(guard_write);
  int exec_error;
  int read_count = read(guard_read, &exec_error, sizeof(int)); 
  /* close read side in parent */
  close(guard_read);
  if (read_count == sizeof(int)) {
    return exec_error;
  }

  p->pid = pid;
  return 0;
}

int velvet_process_spawn(struct velvet *v, char *wd, char **argv, char **envp, struct velvet_process_stream_options streams) {
  /* reuse a single handle to /dev/null for all processes */
  char *filename = argv[0];
  int error = 0;
  if (!strchr(filename, '/')) { 
    char *path = find_path(envp);
    if (path) filename = find_binary(argv[0], path);
  }
  if (!filename) return -ENOENT;

  int in, out, err;
  in = out = err = 0;
  struct streams s = { .in = nullfd, .out = nullfd, .err = nullfd };
  if (error == 0 && streams.in) {
    error = pipe_pair_create_cloexec(&s.in, &in);
  }
  if (error == 0 && streams.out) {
    error = pipe_pair_create_cloexec(&out, &s.out);
  }
  if (error == 0 && streams.err) {
    error = pipe_pair_create_cloexec(&err, &s.err);
  }

  if (error != 0) {
    /* close pipes that opened successfully */
    int to_close[] = {in, out, err, s.in, s.out, s.err};
    for (int i = 0; i < LENGTH(to_close); i++) {
      if (to_close[i] && to_close[i] != nullfd) close(to_close[i]);
    }
    return -error;
  }

  struct velvet_process p = {0};
  error = spawn_process(&p, filename, wd, argv, envp, s);
  /* close client streams on server side on success */
  int to_close[] = {s.in, s.out, s.err};
  for (int i = 0; i < LENGTH(to_close); i++)
    if (to_close[i] && to_close[i] != nullfd) close(to_close[i]);

  if (error != 0) {
    /* close all streams in case of errors */
    int to_close[] = {in, out, err};
    for (int i = 0; i < LENGTH(to_close); i++)
      if (to_close[i] && to_close[i] != nullfd) close(to_close[i]);
    return -error;
  }

  int nonblock[] = {in, out, err};
  for (int i = 0; i < LENGTH(nonblock); i++)
    if (nonblock[i] && nonblock[i] != nullfd) set_nonblocking(nonblock[i]);

  p.id = velvet_next_id();
  p.in = in; p.out = out; p.err = err;
  vec_push(&v->processes, &p);
  assert(p.pid);
  return p.id;
}

void velvet_process_write_stdin(struct velvet *v, struct velvet_process *p, struct u8_slice text) {
  (void)v;
  if (p->pid && !p->stdin_closed) string_push_slice(&p->pending_input, text);
}

void velvet_process_close_stdin(struct velvet *v, struct velvet_process *p) {
  (void)v;
  p->stdin_closed = true;
  /* if there is pending input, flush it before actually closing the file descriptor.
   * otherwise we can close it right way */
  if (p->pending_input.len == 0) {
    if (p->in) {
      close(p->in);
      p->in = 0;
    }
  }
}

void velvet_process_kill_and_destroy_all(struct velvet *v) {
  uint64_t now = get_ms_since_startup();
  struct velvet_process *p;
  /* 1. send SIGTERM to each process */
  vec_foreach(p, v->processes) {
    /* Process could be stopped. Wake it. */
    velvet_process_kill(v, p, SIGCONT);
    velvet_process_kill(v, p, SIGTERM);
  }
  /* 2. migrate the processes to the death list.
   * They will be rudely killed later if they fail to exit. */
  vec_foreach(p, v->processes) {
    p->termination_deadline = now + 1000;
    if (p->in) { close(p->in); p->in = 0; }
    if (p->out) { close(p->out); p->out = 0; }
    if (p->err) { close(p->err); p->err = 0; }
    vec_push(&v->marked_for_death, p);
  }
  vec_clear(&v->processes);

  /* 3. schedule reap */
  velvet_schedule_reap(v);
}

void velvet_process_kill(struct velvet *v, struct velvet_process *p, int signal) {
  (void)v;
  assert(p);
  if (p->pid) {
    kill(p->pid, signal);
  }
}

void velvet_process_destroy(struct velvet_process *p) {
  string_destroy(&p->pending_input);
  if (p->in) close(p->in);
  if (p->out) close(p->out);
  if (p->err) close(p->err);
  p->in = p->out = p->err = 0;
  if (p->pid > 0) {
    /* no longer asking nicely */
    kill(p->pid, SIGKILL);
  }
}
