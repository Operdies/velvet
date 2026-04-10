#include <locale.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include "platform.h"

#include "utils.h"
#include "velvet.h"
#include "velvet_alloc.h"

static int signal_write;
static void signal_handler(int sig, siginfo_t *siginfo, void *context) {
  (void)siginfo, (void)context;
  ssize_t written = write(signal_write, &sig, sizeof(sig));
  if (written < (int)sizeof(sig)) velvet_die("signal write:");
}

static void signal_trap(int sig, siginfo_t *siginfo, void *context) {
  (void)siginfo, (void)context, (void)sig;
  __builtin_trap();
}

static void install_signal_handlers(int *pipes) {
  if (pipe(pipes) < 0) velvet_die("pipe:");

  set_cloexec(pipes[0]);
  set_cloexec(pipes[1]);

  struct sigaction sig_handle = {0};
  sig_handle.sa_sigaction = &signal_handler;
  sig_handle.sa_flags = SA_SIGINFO;

  struct sigaction sig_trap = {0};
  sig_trap.sa_sigaction = &signal_trap;
  sig_trap.sa_flags = SA_SIGINFO;

  if (sigaction(SIGTERM, &sig_handle, NULL) == -1) velvet_die("sigaction:");
  if (sigaction(SIGQUIT, &sig_handle, NULL) == -1) velvet_die("sigaction:");
  if (sigaction(SIGINT, &sig_handle, NULL) == -1) velvet_die("sigaction:");
  if (sigaction(SIGCHLD, &sig_handle, NULL) == -1) velvet_die("sigaction:");
  if (sigaction(SIGHUP, &sig_handle, NULL) == -1) velvet_die("sigaction:");
  if (sigaction(SIGUSR1, &sig_handle, NULL) == -1) velvet_die("sigaction:");
  if (sigaction(SIGUSR2, &sig_handle, NULL) == -1) velvet_die("sigaction:");
  if (sigaction(SIGBUS, &sig_trap, NULL) == -1) velvet_die("sigaction:");

  signal(SIGPIPE, SIG_IGN);
  signal(SIGTTOU, SIG_IGN);
  signal(SIGTTIN, SIG_IGN);
  signal(SIGTSTP, SIG_IGN);
}

static bool file_exists(const char *path) {
  struct stat st;
  return stat(path, &st) == 0;
}

static bool file_is_socket(const char *path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISSOCK(st.st_mode);
}

#define SOCKET_PATH_MAX (int)((sizeof((struct sockaddr_un*)((void*)0))->sun_path) - 1)

static int create_socket(char *path) {
  struct sockaddr_un addr = {.sun_family = AF_UNIX};
  bool success = false;
  int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sockfd == -1) velvet_die("socket:");

  if (!path) {
    char *base = "/tmp/velvet_sock";
    for (int i = 1; i < 100 && !success; i++) {
      snprintf(addr.sun_path, sizeof(addr.sun_path) - 1, "%s.%d", base, i);
      if (file_is_socket(addr.sun_path)) {
        int temp_sock = socket(AF_UNIX, SOCK_STREAM, 0);
        if (connect(temp_sock, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
          // socket is in use -- leave it be
          close(temp_sock);
          continue;
        }
        // socket is not in use -- use it
        unlink(addr.sun_path);
        close(temp_sock);
      }
      success = true;
      break;
    }
  } else {
    if (strcmp(path, "/tmp/velvet-debug") == 0) unlink(path);
    memcpy(addr.sun_path, path, strlen(path));
    success = true;
  }

  if (!success) velvet_die("No free socket.");
  if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
    close(sockfd);
    velvet_fatal("bind:");
  }

  if (listen(sockfd, 1) == -1) {
    unlink(addr.sun_path);
    close(sockfd);
    velvet_fatal("listen:");
  }

  setenv("VELVET", addr.sun_path, true);
  return sockfd;
}

struct velvet_args {
  bool attach;
  bool foreground;
  char *lua;
  char *socket;
  char **cmd;
};

static void vv_send_lua_payload(struct velvet_args args, struct u8_slice payload);
static void vv_send_lua_chunk(struct velvet_args args);
static void vv_attach(struct velvet_args args);

static void usage(char *arg0) {
  printf("Usage:\n  %s [<options>] [<arguments> ...]\n\nOptions:\n"
         "  attach                  Attach to the server at <socket> if present.\n"
         "  detach                  Detach the current terminal from the session\n"
         "  foreground              Start a server as a foreground process.\n"
         "  lua [<file>|-]          Evaluate <file> or stdin on the server.\n"
         "  quit                    Quit the velvet session, killing all windows\n"
         "  reload                  Reload the velvet session, resourcing configs\n"
         "  -S, --socket <socket>   Specify the socket to use instead of guessing or auto-generating it.\n"
         "  -h, --help              Show this help text and exit.\n"
         , arg0);
}

struct velvet_args velvet_parse_args(int argc, char **argv) {
  #define F(name) (strcmp(arg, #name) == 0)
  #define NEXT() ((++i) < argc ? argv[i] : NULL)
  #define EXPECT(value, arg) if (!(value)) velvet_fatal("Option %s expected argument.", arg)
  #define GET(target) target = NEXT(); EXPECT(target, arg)
  struct velvet_args a = {0};
  int n_commands = 0;
  bool nested = getenv("VELVET");

  for (int i = 1; i < argc; i++) {
    char *arg = argv[i];
    if (F(--socket) || F(-S)) {
      if (a.socket) velvet_fatal("--socket specified multiple times.");
      GET(a.socket);
    } else if (F(--help) || F(-h)) {
      usage(argv[0]);
      exit(0);
    } else if (F(lua)) {
      n_commands++;
      a.lua = NEXT();
      if (!a.lua) a.lua = "";
    } else if (F(foreground)) {
      n_commands++;
      if (a.foreground) velvet_fatal("foreground specified multiple times.");
      a.foreground = true;
    } else if (F(attach)) {
      if (nested) velvet_fatal("Nesting velvet sessions is not supported.");
      n_commands++;
      if (a.attach) velvet_fatal("attach specified multiple times.");
      a.attach = true;
    } else if (F(--)) {
      /* double dash terminates argument parsing.
       * Assume the rest of the command line is intended for the lua cli api.
       * This way, it becomes possible to create a lua cli command with
       * the name 'foreground' for example, or any other word which velvet might reserve in the future.
       */
      if (!NEXT()) velvet_fatal("No command specified");
      n_commands++;
      a.cmd = &argv[i];
      break;
    } else {
      n_commands++;
      a.cmd = &argv[i];
      break;
    }
  }

  if (n_commands > 1) velvet_fatal("Multiple commands specified.");

  if (a.lua || a.cmd) {
    if (!a.socket) a.socket = getenv("VELVET");
    if (!a.socket) velvet_fatal("Unable to send command; Either specify the --socket or set $VELVET to a socket path.");
  }

  if (a.socket && !file_is_socket(a.socket) && (a.lua || a.attach)) {
    velvet_fatal("Socket '%s' is not a unix domain socket.", a.socket);
  }

  if (a.socket && strlen(a.socket) > SOCKET_PATH_MAX) {
    velvet_fatal("Socket path max length exceeded. Max: %d", SOCKET_PATH_MAX);
  }

  return a;
}

_Noreturn static void velvet_fast_shutdown(struct velvet *velvet);

/* daemonize by double forking, detaching from the process group, and then exiting the sandwiched parent. */
static bool daemonize(void) {
  int pid = fork();
  if (pid == -1) velvet_die("fork 1:");
  if (pid == 0) {
    /* daemon side */
    /* detach from controlling terminal */
    if (setsid() < 0)
      velvet_die("setsid:");

    pid = fork();
    if (pid == -1) velvet_die("fork 2:");
    if (pid == 0) return true;
    /* exit the middle process. This process is reaped in the waitpid() call below. */
    exit(0);
  } else {
    /* reap the fork. Otherwise it becomes a zombie process */
    waitpid(pid, NULL, 0);
    return false;
  }
}

static void vv_send_cmd(struct velvet_args args) {
  struct string lua_buf = {0};
  string_push_format_slow(&lua_buf, "return vv.cli.execute([==[%s]==], {", args.cmd[0]);
  for (char **cmd = &args.cmd[1]; *cmd; cmd++) {
    string_push_format_slow(&lua_buf, "[==[%s]==], ", *cmd);
  }
  string_push_cstr(&lua_buf, "})\n");

  vv_send_lua_payload(args, u8_slice_from_string(lua_buf));
  string_destroy(&lua_buf);
}

int main(int argc, char **argv) {
  setlocale(LC_CTYPE, "");
  setenv("TERM", "xterm-256color", true);
  struct velvet_args args = velvet_parse_args(argc, argv);

  if (args.attach) {
    if (getenv("VELVET")) {
      velvet_fatal("Unable to attach; terminal is already in a velvet session.");
      return 1;
    }
    vv_attach(args);
    return 0;
  }

  if (args.lua) {
    vv_send_lua_chunk(args);
    return 0;
  }

  if (args.cmd) {
    vv_send_cmd(args);
    return 0;
  }

  // if (getenv("VELVET")) velvet_fatal("Nesting velvet is not supported.");
  int sock_fd = create_socket(args.socket);
  args.socket = getenv("VELVET");

  // Since we are not connecting to a server, that means we are creating a new server.
  // The server should be detached from the current process hierarchy.
  // We do this with a classic double fork()
  if (!args.foreground) {
    struct rect ws = {0};
    platform_get_winsize(&ws);
    if (ws.height == 0 || ws.width == 0) {
      fprintf(stderr, "Error getting terminal size. Exiting.\n");
      return 1;
    }

    /* we are starting a new session. daemonize and connect to the server. */
    bool is_daemon = daemonize();
    if (!is_daemon) {
      /* close the socket on this side and attach to the server. */
      close(sock_fd);
      /* attach to the server */
      vv_attach(args);
      return 0;
    }
  } else {
    printf("Server listening at %s\n", getenv("VELVET"));
  }

  int signal_pipes[2];
  install_signal_handlers(signal_pipes);
  signal_write = signal_pipes[1];
  // detached child process of exited parent

  if (!args.foreground) {
    close(STDIN_FILENO);
    // redirect all output/error to log files
    char *outpath = "/tmp/velvet.stdout";
    char *errpath = "/tmp/velvet.stderr";
    int new_stderr = open(errpath, O_TRUNC | O_CREAT | O_WRONLY | O_CLOEXEC, S_IRWXU);
    int new_stdout = open(outpath, O_TRUNC | O_CREAT | O_WRONLY | O_CLOEXEC, S_IRWXU);
    dup2(new_stderr, STDERR_FILENO);
    dup2(new_stdout, STDOUT_FILENO);
  }

  char startup_directory[PATH_MAX];
  getcwd(startup_directory, PATH_MAX - 1);

  struct velvet velvet = {
      .scene = velvet_scene_default,
      .input = velvet_input_default,
      .sessions = vec(struct velvet_session),
      .stored_strings = vec(struct velvet_kvp),
      .socket = sock_fd,
      .event_loop = io_default,
      .signal_read = signal_pipes[0],
      .daemon = !args.foreground,
      .startup_directory = startup_directory,
  };

  velvet_loop(&velvet);
  velvet_fast_shutdown(&velvet);
}

_Noreturn static void velvet_fast_shutdown(struct velvet *velvet) {
  // 1. Notify all attached clients to detach
  if (velvet->socket) {
    struct velvet_session *session;
    vec_foreach(session, velvet->sessions) {
      if (session->socket) {
        uint8_t quit = 'Q';
        write(session->socket, &quit, 1);
      }
    }

    close(velvet->socket);
  }

  // 2. Remove socket file from filesystem
  char *sockpath = getenv("VELVET");
  if (sockpath) {
    unlink(sockpath);
  }

  // 3. Notify child processes
  struct velvet_window *h;
  vec_foreach(h, velvet->scene.windows) {
    if (h->pty > 0) {
      pid_t pgid = tcgetpgrp(h->pty);
      if (pgid > 0) {
        kill(-pgid, SIGCONT);
        kill(-pgid, SIGHUP);
      } else if (h->pid > 0) {
        kill(h->pid, SIGCONT);
        kill(h->pid, SIGHUP);
      }
    }
  }

  // 4. Exit
  exit(0);
}

static void attach_sighandler(int sig, siginfo_t *siginfo, void *context) {
  (void)siginfo, (void)context;
  ssize_t written = write(signal_write, &sig, sizeof(sig));
  if (written < (int)sizeof(sig)) velvet_die("signal write:");
}

static void vv_attach_update_size(int sockfd) {
  static char cmdbuf[256];
  struct rect size;
  platform_get_winsize(&size);
  char codebuf[200];
  int n_codebuf = snprintf(codebuf,
                           sizeof(codebuf),
                           "vv.api.session_set_options(0, { lines = %d, columns = %d, y_pixel = %d, x_pixel = %d })\n",
                           size.height,
                           size.width,
                           size.y_pixel,
                           size.x_pixel);
  int n = snprintf(cmdbuf, sizeof(cmdbuf), "%d%s", n_codebuf, codebuf);
  struct u8_slice s = {.content = (uint8_t *)cmdbuf, .len = n};
  io_write(sockfd, s);
}

static int socket_send_files(int sockfd, int *fds, int n_fds, void *payload, int n_payload) {
  char cmsgbuf[CMSG_SPACE(sizeof(int) * n_fds)];

  struct msghdr msg = {.msg_iov = &(struct iovec){.iov_base = payload, .iov_len = n_payload}, .msg_iovlen = 1};

  msg.msg_control = cmsgbuf;
  msg.msg_controllen = sizeof(cmsgbuf);
  struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
  cmsg->cmsg_len = CMSG_LEN(sizeof(int) * n_fds);
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_RIGHTS;
  memcpy(CMSG_DATA(cmsg), fds, sizeof(int) * n_fds);

  return sendmsg(sockfd, &msg, 0);
}

static void vv_attach_handshake(int sockfd, struct rect size, int input_fd, int output_fd) {
  static char cmdbuf[256];
  int fds[2] = {input_fd, output_fd};
  bool no_repeat_wide_chars = false;
  char *term;
  if ((term = getenv("TERM_PROGRAM")) && strcmp(term, "Apple_Terminal") == 0) {
    no_repeat_wide_chars = true;
  }

  char codebuf[200];
  int n_codebuf = snprintf(codebuf,
                           sizeof(codebuf),
                           "vv.api.session_set_options(0, { lines = %d, columns = %d, y_pixel = %d, x_pixel = %d, "
                           "supports_repeating_multibyte_characters = %s })\n",
                           size.height,
                           size.width,
                           size.y_pixel,
                           size.x_pixel,
                           no_repeat_wide_chars ? "false" : "true");
  int n_cmdbuf = snprintf(cmdbuf, sizeof(cmdbuf), "%d%s", n_codebuf, codebuf);

  if (socket_send_files(sockfd, fds, 2, cmdbuf, n_cmdbuf) == -1) {
    close(sockfd);
    velvet_die("sendmsg:");
  }
}

static int vv_connect(char *vv_socket) {
  int sockfd;
  // Create the client socket
  sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sockfd == -1) {
    velvet_fatal("socket:");
  }

  struct sockaddr_un addr = {.sun_family = AF_UNIX};

  if (!vv_socket) {
    char *base = "/tmp/velvet_sock";
    bool connected = false;
    for (int i = 1; i < 100 && !connected; i++) {
      snprintf(addr.sun_path, sizeof(addr.sun_path) - 1, "%s.%d", base, i);
      if (file_exists(addr.sun_path)) {
        if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
          // if the socket exists but we cannot connect,
          // delete it. This is likely from a dead server which did not shut down correctly.
          unlink(addr.sun_path);
        } else {
          connected = true;
        }
      }
    }
    if (!connected) {
      terminal_reset();
      fprintf(stderr, "No sessions.\n");
      exit(1);
    }
  } else {
    strncpy(addr.sun_path, vv_socket, sizeof(addr.sun_path) - 1);
    if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
      close(sockfd);
      velvet_fatal("connect:");
    }
  }
  return sockfd;
}

struct vv_source {
  int source, sock;
  struct io loop;
};

static void vv_send_lua_payload(struct velvet_args args, struct u8_slice payload) {
  char buf[4096];
  int sockfd = vv_connect(args.socket);

  /* overcommit 2x the memory we need. This is not wasteful because the pages don't get
   * committed before they are written, but required because the allocator needs a small amount of
   * space for metadata. This is basically a fix for the edge case where payload.len is exactly equal to the system
   * page size. */
  struct velvet_alloc *shmem = velvet_alloc_shmem_create(2 * payload.len);
  char *chunk = shmem->calloc(shmem, payload.len, 1);
  memcpy(chunk, payload.content, payload.len);
  struct vv_lua_payload magic_header = {
      /* recipient needs to know the offset of the allocation */
      .chunk_offset = chunk - (char *)shmem,
      .chunk_length = payload.len,
      .magic = VV_LUA_MAGIC,
  };

  int fd = velvet_alloc_shmem_get_fd(shmem);
  if (socket_send_files(sockfd, &fd, 1, &magic_header, sizeof(magic_header)) == -1) {
    velvet_fatal("send mmap:");
  }
  /* the server owns the mmap now, so we can close it */
  velvet_alloc_shmem_destroy(shmem, fd);

  int n = 0;
  do {
    n = 0;
    struct pollfd pfd = {.fd = sockfd, .events = POLLIN};
    /* if polling takes more than a second, assume the server is stuck --
     * this could be caused by trying to write to the window hosting this very 
     * process, so let's be kind and unfreeze it */
    if (poll(&pfd, 1, 1000) > 0) {
      n = read(sockfd, buf, sizeof(buf));
      printf("%.*s", n, buf);
    }
  } while (n > 0);
  close(sockfd);
}

static void read_fd_to_file(int fd, struct string *s) {
  char buf[4096];
  ssize_t n;
  while ((n = read(fd, buf, sizeof(buf))) > 0)
    string_push_slice(s, (struct u8_slice){.content = (uint8_t *)buf, .len = (size_t)n});
  string_ensure_null_terminated(s);
}

static void vv_send_lua_chunk(struct velvet_args args) {
  char buf[4096];

  struct string stdin_buf = {0};
  if (args.lua && args.lua[0] && args.lua[0] != '-') {
    struct u8_slice s = u8_slice_from_cstr(args.lua);
    if (u8_slice_starts_with_cstr(s, "/dev/fd/")) {
      /* this file is only available here -- probably provided in the shell with pipe redirection or similar
       * Just read the content instead. */
      int fd = open(args.lua, O_RDONLY);
      read_fd_to_file(fd, &stdin_buf);
      close(fd);
    } else {
      if (!realpath(args.lua, buf) || !file_exists(args.lua)) {
        velvet_fatal("Could not open %s:", args.lua);
      }
      string_push_format_slow(&stdin_buf, "return dofile([[%s]])", buf);
    }
  } else {
    read_fd_to_file(STDIN_FILENO, &stdin_buf);
  }

  vv_send_lua_payload(args, string_as_u8_slice(stdin_buf));
  string_destroy(&stdin_buf);
}

struct vv_attach_context {
  int socket;
};

_Noreturn static void quit(char *reason) {
  terminal_reset();
  printf("[%s]\n", reason);
  exit(0);
}

static void vv_attach_on_output(struct io_source *src, struct u8_slice str) {
  (void)src;
  io_write(STDOUT_FILENO, str);
}

static void vv_attach_on_socket(struct io_source *src, struct u8_slice str) {
  (void)src;
  if (str.len == 0) quit("Shutdown");
  if (str.len == 1 && str.content[0] == 'Q') quit("Shutdown");
  if (str.len == 1 && str.content[0] == 'D') quit("Detached");
}

static void vv_attach_on_signal(struct io_source *src, struct u8_slice str) {
  struct vv_attach_context *ctx = src->data;
  // 1. Dispatch any pending signals
  struct int_slice signals = {.content = (int *)str.content, .n = str.len / 4};
  for (size_t i = 0; i < signals.n; i++) {
    int signal = signals.content[i];
    switch (signal) {
    case SIGWINCH: {
      vv_attach_update_size(ctx->socket);
    } break;
    default: quit("Shutdown"); break;
    }
  }
}

static int stdin_flags = 0;
static int stdout_flags = 0;
static void restore_flags() {
  fcntl(STDIN_FILENO, F_SETFL, stdin_flags);
  fcntl(STDOUT_FILENO, F_SETFL, stdout_flags);
}

static void ensure_input_output_blocking() {
  stdin_flags = fcntl(STDIN_FILENO, F_GETFL);
  if (stdin_flags == -1 || fcntl(STDIN_FILENO, F_SETFL, stdin_flags & ~O_NONBLOCK) == -1) {
    velvet_die("fcntl:");
  }

  stdout_flags = fcntl(STDOUT_FILENO, F_GETFL);
  if (stdout_flags == -1 || fcntl(STDOUT_FILENO, F_SETFL, stdout_flags & ~O_NONBLOCK) == -1) {
    velvet_die("fcntl:");
  }
}

/* TODO: This loop sucks.
 * Update it to use `struct io` and proper signal handling.
 */
static void vv_attach(struct velvet_args args) {
  int signal_pipes[2];
  if (pipe(signal_pipes) < 0) velvet_die("pipe:");
  signal_write = signal_pipes[1];
  int signal_read = signal_pipes[0];

  struct sigaction sa = {0};
  sa.sa_sigaction = &attach_sighandler;
  sa.sa_flags = SA_RESTART;

  if (sigaction(SIGWINCH, &sa, NULL) == -1) velvet_die("sigaction:");

  struct rect ws;
  platform_get_winsize(&ws);

  terminal_setup(restore_flags);

  /* client logic depends on stdout being blocking for clean writes. */
  ensure_input_output_blocking();

  int sockfd = vv_connect(args.socket);

  int output_pipe[2];
  if (pipe(output_pipe) < 0) velvet_fatal("pipe:");
  vv_attach_handshake(sockfd, ws, STDIN_FILENO, output_pipe[1]);

  struct io io = io_default;
  struct vv_attach_context ctx = { .socket = sockfd };

  while (true) {
    io_clear_sources(&io);
    struct io_source signal_source = {
        .data = &ctx,
        .fd = signal_read,
        .events = IO_SOURCE_POLLIN,
        .on_read = vv_attach_on_signal,
    };
    io_add_source(&io, signal_source);
    struct io_source output_source = {
        .data = &ctx,
        .fd = output_pipe[0],
        .events = IO_SOURCE_POLLIN,
        .on_read = vv_attach_on_output,
    };
    io_add_source(&io, output_source);
    struct io_source socket_source = {
        .data = &ctx,
        .fd = sockfd,
        .events = IO_SOURCE_POLLIN,
        .on_read = vv_attach_on_socket,
    };
    io_add_source(&io, socket_source);
    io_dispatch(&io);
  }
}
