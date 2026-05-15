#include "velvet.h"
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>
#include "collections.h"
#include "velvet_process.h"
#include <fcntl.h>

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

static const char *find_binary(const char *name, const char *path) {
  static char temp[PATH_MAX] = {0};
  char *result = NULL;
  char *copy = strdup(path);
  char *p = copy;
  while (p) {
    char *next = strchr(p, ':');
    if (next) *next = 0;
    snprintf(temp, PATH_MAX - 1, "%s/%s", p, name);
    int mask = F_OK | R_OK | X_OK;
    if (access(temp, mask) == 0) {
      result = temp;
      break;
    }
    if (next) p = next + 1;
    else break;
  }
  free(copy);
  return result;
}

_Noreturn static void process_setup_child(int error_pipe, const char *wd,
                                          const char *filename,
                                          char *const *argv, char *const *envp,
                                          int in, int out, int err) {
  if (wd) {
    if (chdir(wd) == -1) {
      ERROR("chdir:");
    }
  } else {
    char *home = getenv("HOME");
    if (home) chdir(home);
  }
  if (!in) in = open("/dev/null", O_RDONLY);
  if (!out) out = open("/dev/null", O_WRONLY);
  if (!err) err = open("/dev/null", O_WRONLY);
  dup2(in, STDIN_FILENO);
  dup2(out, STDOUT_FILENO);
  dup2(err, STDERR_FILENO);
  close(in); close(out); close(err);
  /* close read side in fork */

  if (envp) {
    execve(filename, argv, envp);
  } else {
    execv(filename, argv);
  }
  int exec_error = errno;
  write(error_pipe, &exec_error, sizeof(int));
  velvet_die("execvp:");
  /* write side automatically cleaned up in child */
}

struct pair {
  int r, w;
};

static struct pair mypipe(void) {
  int fds[2];
  if (pipe(fds) < 0) velvet_die("pipe:");
  set_cloexec(fds[0]);
  set_cloexec(fds[1]);
  return (struct pair){ .r = fds[0], .w = fds[1] };
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
  /* fork sequence largely copied from velvet_scene.c. The same principles apply;
  * block signal generation before forking so the child cannot write to velvet's signal pipes. */
  sigset_t block, sighandler, trash_signalset;
  sigfillset(&block);
  sigprocmask(SIG_BLOCK, &block, &sighandler);

  struct pair guard = mypipe();
  pid_t pid = fork();

  if (pid == 0) {
    restore_signals();
  }

  sigprocmask(SIG_SETMASK, &sighandler, &trash_signalset);

  if (pid < 0) {
    close(guard.r); close(guard.w);
    ERROR("Unable to spawn process:");
    return errno;
  }

  int exec_error;

  if (pid == 0) {
    /* close read side in child */
    close(guard.r);
    process_setup_child(guard.w, wd, filename, argv, envp, streams.in, streams.out, streams.err);
    /* child does not return here */
  }

  /* Close write side in parent. Otherwise read(rw[0]) will block. */
  close(guard.w);
  int read_count = read(guard.r, &exec_error, sizeof(int)); 
  /* close read side in parent */
  close(guard.r);
  if (read_count == sizeof(int)) {
    return exec_error;
  }

  p->pid = pid;
  return 0;
}

int velvet_process_spawn(struct velvet *v, char *wd, char **argv, char **envp, struct velvet_process_stream_options streams) {
  const char *filename = argv[0];
  if (!strchr(filename, '/')) { 
    char *path = find_path(envp);
    if (path) filename = find_binary(argv[0], path);
  }
  if (!filename) return -ENOENT;

  int in, out, err;
  in = out = err = 0;
  struct streams s = {0};
  if (streams.in) {
    struct pair p = mypipe();
    in = p.w;
    s.in = p.r;
  }
  if (streams.out) {
    struct pair p = mypipe();
    out = p.r;
    s.out = p.w;
  }
  if (streams.err) {
    struct pair p = mypipe();
    err = p.r;
    s.err = p.w;
  }

  struct velvet_process p = {0};
  int error = spawn_process(&p, filename, wd, argv, envp, s);
  /* close client streams on server side on success */
  int to_close[] = {s.in, s.out, s.err};
  for (int i = 0; i < LENGTH(to_close); i++)
    if (to_close[i]) close(to_close[i]);

  if (error > 0) {
    /* close all streams in case of errors */
    int to_close[] = {in, out, err};
    for (int i = 0; i < LENGTH(to_close); i++)
      if (to_close[i]) close(to_close[i]);
    return -error;
  }

  int nonblock[] = {in, out, err};
  for (int i = 0; i < LENGTH(nonblock); i++)
    if (nonblock[i]) set_nonblocking(nonblock[i]);

  p.id = velvet_next_id();
  p.in = in; p.out = out; p.err = err;
  if (streams.in) p.stdin_closed = true;
  vec_push(&v->processes, &p);
  return p.id;
}

void velvet_process_write_stdin(struct velvet *v, struct velvet_process *p, struct u8_slice text) {
  (void)v;
  if (!p->stdin_closed) string_push_slice(&p->pending_input, text);
}

void velvet_process_close_stdin(struct velvet *v, struct velvet_process *p) {
  (void)v;
  p->stdin_closed = true;
}

void velvet_process_kill_all(struct velvet *v) {
  struct velvet_process *p;
  vec_rforeach(p, v->processes) {
    velvet_process_kill(v, p);
  }
}

void velvet_process_kill(struct velvet *v, struct velvet_process *p) {
  assert(p);
  assert(p->pid);
  kill(p->pid, SIGTERM);
  int status;
  pid_t result = waitpid(p->pid, &status, WNOHANG);
  if (result == -1) {
    /* This is fine to ignore. It just means the process did not exit
     * immediately. We will handle this later. */
    p->destroy_pending = true;
  } else {
    p->pid = 0;
    p->exit_code = WEXITSTATUS(status);;
    velvet_process_destroy(v, p);
  }
}

void velvet_process_destroy(struct velvet *v, struct velvet_process *p) {
  struct velvet_api_process_exit_event_args args = {.id = p->id,
                                                    .exit_code = p->exit_code};
  string_destroy(&p->pending_input);
  if (p->in) close(p->in);
  if (p->out) close(p->out);
  if (p->err) close(p->err);
  if (p->pid > 0) {
    /* no longer asking nicely */
    kill(p->pid, SIGKILL);
    p->pid = 0;
    /* processes killed by a signal exit with 128+signal.
     * Although we don't wait to reap the process here, we assume SIGKILL does
     * its job, though it can still fail for defunct processes. */
    args.exit_code = 128 + SIGKILL;
  }
  vec_remove(&v->processes, p);
  velvet_api_raise_process_exited(v, args);
}
