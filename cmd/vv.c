#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include "platform.h"
#include <stdio.h>
#include <dirent.h>

#include "utils.h"
#include "velvet.h"
#include "velvet_alloc.h"
#include "velvet_process.h"

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
  sig_handle.sa_flags = SA_SIGINFO | SA_RESTART;

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

static void ensure_parent_dir_exists(char *base) {
  char *last_slash = strrchr(base, '/');
  if (last_slash) {
    *last_slash = 0;
    if (!file_exists(base)) {
      ensure_parent_dir_exists(base);
      if (mkdir(base, 0700) == -1) velvet_fatal("mkdir %s:", base);
    }
    *last_slash = '/';
  }
}

static int create_socket(char *name) {
  struct sockaddr_un addr = {.sun_family = AF_UNIX};
  int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sockfd == -1) velvet_die("socket:");

  char namebuf[256] = {0};
  if (name) {
    if (name[0] == '/') {
    }
    snprintf(namebuf, LENGTH(namebuf) - 1, "%s", name);
  } else {
    snprintf(namebuf, LENGTH(namebuf) - 1, "sock.%d", getpid());
  }

  struct string path = {0};
  string_joinpath(&path, getenv("HOME"), ".local", "share", "velvet", "sockets",
                  namebuf);
  string_ensure_null_terminated(&path);
  ensure_parent_dir_exists((char*)path.content);
  if (path.len >= LENGTH(addr.sun_path)) velvet_fatal("Socket name too long.");

  snprintf(addr.sun_path, LENGTH(addr.sun_path), "%.*s", (int)path.len, (char*)path.content);
  if (file_exists((char *)path.content)) {
    if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) != -1) {
      close(sockfd);
      velvet_fatal("Server name %s is already in use.", namebuf);
    }
  }
  unlink((char *)path.content);
  string_destroy(&path);

  if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
    close(sockfd);
    velvet_fatal("bind:");
  }

  if (listen(sockfd, 1) == -1) {
    unlink(addr.sun_path);
    close(sockfd);
    velvet_fatal("listen:");
  }

  char buf[sizeof(addr.sun_path)];
  snprintf(buf, sizeof(buf), "%s", addr.sun_path);
  char *lastslash = strrchr(buf, '/');
  setenv("VELVET", lastslash + 1, true);
  return sockfd;
}

struct velvet_args {
  bool attach;
  bool foreground;
  bool clean;
  char *lua;
  char *socket;
  char *cmd;
  char **positional;
};

static int vv_send_lua_payload(struct velvet_args args, struct u8_slice payload);
static int vv_send_lua_chunk(struct velvet_args args);
static void vv_attach(struct velvet_args args);

static void usage(char *arg0) {
  printf("Usage:\n  %s [<options>] [<arguments> ...] [-- [<positional arguments>]]\n\nOptions:\n"
         "  attach                       Attach to the server at <socket> if present\n"
         "  detach                       Detach the current terminal from the server\n"
         /* foreground intentionally not documented because it is exclusively a debugging feature.
         "  foreground              Start a server as a foreground process.\n"
         */
         "  lua [<file>|-] [--] [<args>] Execute <file> or input on the server. <args> will be exposed to the script in the global <arg>\n"
         "  quit                         Quit the velvet server, killing all windows.\n"
         "  reload                       Restart the lua VM and source configs.\n"
         "  spawn <program> [<args>]     Spawn <program> with <args>\n"
         "  -S, --socket <name>          Specify the socket to use instead of guessing or auto-generating it\n"
         "  --clean                      Start a new server with the stock config.\n"
         "  --version                    Print version and exit\n"
         "  -h, --help                   Show this help text and exit\n"
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
      if (strlen(a.socket) > 200) velvet_fatal("Socket name too long. Max 200 characters.");
    } else if (F(--clean)) {
      a.clean = true;
    } else if (F(--help) || F(-h)) {
      usage(argv[0]);
      exit(0);
    } else if (F(--version) || F(-v)) {
      printf("velvet %s\n", VELVET_VERSION);
      exit(0);
    } else if (F(lua)) {
      n_commands++;
      a.lua = NEXT();
      if (a.lua) {
        /* read from stdin */
        if (strcmp(a.lua, "--") == 0) a.lua = "-";
        a.positional = &argv[i+1];
        break;
      } else {
        /* read from stdin */
        a.lua = "-";
      }
    } else if (F(foreground)) {
      n_commands++;
      if (a.foreground) velvet_fatal("foreground specified multiple times.");
      a.foreground = true;
    } else if (F(attach)) {
      if (nested) velvet_fatal("Nesting velvet servers is not supported.");
      n_commands++;
      if (a.attach) velvet_fatal("attach specified multiple times.");
      a.attach = true;
    } else if (F(--)) {
      if (!NEXT()) velvet_fatal("No arguments specified after --");
      a.positional = &argv[i];
      break;
    } else {
      if (a.lua) {
        a.positional = &argv[i];
      } else {
        n_commands++;
        a.cmd = argv[i];
        a.positional = &argv[i + 1];
      }
      break;
    }
  }

  if (n_commands > 1) velvet_fatal("Multiple commands specified.");

  if (a.lua || a.cmd) {
    if (!a.socket) a.socket = getenv("VELVET");
    if (!a.socket) velvet_fatal("Unable to send command; Either specify the --socket or set $VELVET to a socket path.");
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

static int vv_send_cmd(struct velvet_args args) {
  struct string lua_buf = {0};
  string_push_format_slow(&lua_buf, "return vv.cli.execute([==[%s]==]", args.cmd);
  for (char **cmd = args.positional; cmd && *cmd; cmd++) {
    string_push_format_slow(&lua_buf, ", [==[%s]==]", *cmd);
  }
  string_push_cstr(&lua_buf, ")\n");

  int success = vv_send_lua_payload(args, u8_slice_from_string(lua_buf));
  string_destroy(&lua_buf);
  return success;
}

static char **main_argv;
int main(int argc, char **argv) {
  main_argv = argv;
  setlocale(LC_CTYPE, "");
  setenv("TERM", "xterm-256color", true);
  struct velvet_args args = velvet_parse_args(argc, argv);

  if (args.attach) {
    if (getenv("VELVET")) {
      velvet_fatal("Unable to attach. This process is already attached to a velvet server.");
      return 1;
    }
    vv_attach(args);
    return 0;
  }

  if (args.lua) {
    return vv_send_lua_chunk(args);
  }

  if (args.cmd) {
    return vv_send_cmd(args);
  }

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

    /* we are starting a new server. daemonize and connect to the server. */
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
      .clients = vec(struct velvet_client),
      .coroutines = vec(struct velvet_coroutine),
      .processes = vec(struct velvet_process),
      .stored_strings = vec(struct velvet_kvp),
      .socket = sock_fd,
      .event_loop = io_default,
      .signal_read = signal_pipes[0],
      .daemon = !args.foreground,
      .startup_directory = startup_directory,
      .positional_args = args.positional,
      .arg0 = argv[0],
      .clean = args.clean,
  };

  velvet_loop(&velvet);
  velvet_fast_shutdown(&velvet);
}

_Noreturn static void velvet_fast_shutdown(struct velvet *velvet) {
  // 1. Notify all attached clients to detach
  if (velvet->socket) {
    struct velvet_client *client;
    vec_foreach(client, velvet->clients) {
      if (client->socket) {
        uint8_t quit = 'Q';
        write(client->socket, &quit, 1);
      }
    }

    close(velvet->socket);
  }

  // 2. Remove socket file from filesystem
  char *sockpath = getenv("VELVET");
  if (sockpath) {
    struct string pathbuf = {0};
    string_joinpath(&pathbuf, getenv("HOME"), ".local", "share", "velvet", "sockets", sockpath);
    string_ensure_null_terminated(&pathbuf);
    unlink((char*)pathbuf.content);
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
                           "vv.api.client_set_options(0, { lines = %d, columns = %d, y_pixel = %d, x_pixel = %d })\n",
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
  bool no_repeat = false;
  bool no_repeat_wide_chars = false;
  char *term, *emulator;
  if ((term = getenv("TERM_PROGRAM")) && strcmp(term, "Apple_Terminal") == 0) {
    no_repeat_wide_chars = true;
  }
  if ((emulator = getenv("TERMINAL_EMULATOR")) && strcmp(emulator, "JetBrains-JediTerm") == 0) {
    no_repeat = true;
  }

  char codebuf[1024];
  int n_codebuf = snprintf(codebuf,
                           sizeof(codebuf),
                           "vv.api.client_set_options(0, { lines = %d, columns = %d, y_pixel = %d, x_pixel = %d, "
                           "supports_repeating_characters = %s, "
                           "supports_repeating_multibyte_characters = %s })\n",
                           size.height,
                           size.width,
                           size.y_pixel,
                           size.x_pixel,
                           no_repeat ? "false" : "true",
                           no_repeat_wide_chars ? "false" : "true");
  int n_cmdbuf = snprintf(cmdbuf, sizeof(cmdbuf), "%d%s", n_codebuf, codebuf);

  if (socket_send_files(sockfd, fds, 2, cmdbuf, n_cmdbuf) == -1) {
    close(sockfd);
    velvet_die("sendmsg:");
  }
}

static int vv_connect(char *vv_socket) {
  int sockfd;
  bool connected = false;
  // Create the client socket
  sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
  set_cloexec(sockfd);
  if (sockfd == -1) {
    velvet_fatal("socket:");
  }

  struct sockaddr_un addr = {.sun_family = AF_UNIX};

  struct string socket_path = {0};
  string_joinpath(&socket_path, getenv("HOME"), ".local", "share", "velvet", "sockets");
  string_ensure_null_terminated(&socket_path);
  if (vv_socket) {
    /* if the specified socket starts with a '/', assume it is a fully qualified path */
    if (vv_socket[0] == '/') {
      snprintf(addr.sun_path, LENGTH(addr.sun_path), "%s", vv_socket);
    } else {
      /* otherwise assume it refers to a named server */
      string_push_format_slow(&socket_path, "/%s", vv_socket);
      snprintf(addr.sun_path, LENGTH(addr.sun_path), "%.*s",
               (int)socket_path.len, (char *)socket_path.content);
    }
    if (!file_is_socket(addr.sun_path)) {
      terminal_reset();
      fprintf(stderr, "No server named '%s'.\n", vv_socket);
      exit(1);
    }
    if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
      if (errno == ECONNREFUSED) unlink(addr.sun_path);
      close(sockfd);
      velvet_fatal("connect:");
    }
    connected = true;
  } else {
    DIR *dir = opendir((char *)socket_path.content);
    if (!dir) {
      if (errno == ENOENT)
        velvet_fatal("no servers");
      velvet_fatal("Unable to enumerate servers:");
    }

    struct dirent *entry;
    struct string pathbuf = {0};
    while ((entry = readdir(dir)) != NULL) {
      string_clear(&pathbuf);
      string_joinpath(&pathbuf, (char*)socket_path.content, entry->d_name);
      string_ensure_null_terminated(&pathbuf);
      const char *name = entry->d_name;

      // Skip . and ..
      if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        continue;

      if (file_is_socket((char *)pathbuf.content)) {
        snprintf(addr.sun_path, LENGTH(addr.sun_path), "%.*s", (int)pathbuf.len,
                 (char *)pathbuf.content);
        if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
          /* if the socket exists but we cannot connect, delete it. This is
           * likely from a dead server which did not shut down correctly. */
          unlink(addr.sun_path);
        } else {
          connected = true;
          break;
        }
      }
    }
    string_destroy(&pathbuf);
    closedir(dir);
  }
  string_destroy(&socket_path);
  if (!connected) velvet_fatal("No servers");
  return sockfd;
}

static void restore_streams(void);
static void ensure_streams_blocking(void);

struct velvet_lua_payload_context {
  bool quit;
  bool stdin_closed;
  enum velvet_coroutine_exit_code exit_code;
};

static void vv_lua_on_output(struct io_source *src, struct u8_slice str) {
  (void)src;
  io_write(STDOUT_FILENO, str);
}

static bool color_stderr = false;
static void vv_lua_on_error(struct io_source *src, struct u8_slice str) {
  (void)src;
  struct u8_slice red = u8_slice_from_cstr("\x1b[31m");
  struct u8_slice reset = u8_slice_from_cstr("\x1b[0m");

  if (color_stderr)
    io_write(STDERR_FILENO, red);
  io_write(STDERR_FILENO, str);
  if (color_stderr)
    io_write(STDERR_FILENO, reset);
}
static void vv_lua_on_input(struct io_source *src, struct u8_slice str) {
  (void)src;
  (void)str;
  struct velvet_lua_payload_context *ctx = src->data;
  if (str.len == 0) ctx->stdin_closed = true;
}
static void vv_lua_on_socket(struct io_source *src, struct u8_slice str) {
  struct velvet_lua_payload_context *ctx = src->data;
  if (!ctx->quit) {
    /* POLLHUP can trigger after the server has already sent its 
     * exit code. If we already entered once, the first exit 
     * code is correct. */
    ctx->quit = true;
    if (str.len == 0) {
      ctx->exit_code = VELVET_COROUTINE_SERVER_EXITED;
    } else if (str.len == sizeof(int)) {
      ctx->exit_code = *(int*)str.content;
    } else {
      ctx->exit_code = VELVET_COROUTINE_UNEXPECTED_WRITE;
    }
  }
}

static int vv_send_lua_payload(struct velvet_args args, struct u8_slice payload) {
  int sockfd = vv_connect(args.socket);

  /* overcommit 2x the memory we need. This is not wasteful because the pages don't get
   * committed before they are written, but required because the allocator needs a small amount of
   * space for metadata. This is basically a fix for the edge case where payload.len is exactly equal to the system
   * page size. */

  size_t combined_arg_length = 0;
  struct vec arglist = vec(struct u8_slice);
  char *arg0 = args.cmd ? args.cmd : args.lua;
  if (args.lua && (!args.lua[0] || strcmp(args.lua, "-") == 0)) arg0 = "<stdin>";
  struct u8_slice arg_slice = u8_slice_from_cstr(arg0);
  vec_push(&arglist, &arg_slice);
  combined_arg_length = arg_slice.len;

  for (char **pos = args.positional; pos && *pos; pos++) {
    arg_slice = u8_slice_from_cstr(*pos);
    vec_push(&arglist, &arg_slice);
    combined_arg_length += arg_slice.len;
  }

  struct velvet_alloc *shmem = velvet_alloc_shmem_create(2 * (payload.len + combined_arg_length));
  char *chunk = shmem->calloc(shmem, payload.len, 1);
  memcpy(chunk, payload.content, payload.len);
  struct vv_lua_payload magic_header = {
      /* recipient needs to know the offset of the allocation */
      .chunk_offset = chunk - (char *)shmem,
      .chunk_length = payload.len,
      .magic = VV_LUA_MAGIC,
      .args_count = arglist.length,
  };

  magic_header.args_count = arglist.length;
  size_t *string_offsets = shmem->calloc(shmem, arglist.length, sizeof(size_t));
  magic_header.args_offset = (char*)string_offsets - (char*) shmem;

  for (size_t i = 0; i < arglist.length; i++) {
    struct u8_slice *s = vec_nth(arglist, i);
    char *argi = shmem->calloc(shmem, s->len + 1, sizeof(char));
    strcpy(argi, (char*)s->content);
    argi[s->len] = 0;
    string_offsets[i] = argi - (char *)shmem;
  }
  vec_destroy(&arglist);

  char cwd[PATH_MAX];
  getcwd(cwd, PATH_MAX - 1);
  char *cwd2 = shmem->calloc(shmem, strlen(cwd) + 1, sizeof(char));
  strcpy(cwd2, cwd);
  magic_header.cwd_offset = cwd2 - (char *)shmem;

  int out_fd[2];
  int err_fd[2];
  if (pipe(out_fd) == -1) velvet_die("pipe:");
  if (pipe(err_fd) == -1) velvet_die("pipe:");
  int shmem_fd = velvet_alloc_shmem_get_fd(shmem);
  int fds[] = { shmem_fd, out_fd[1], err_fd[1] };
  if (socket_send_files(sockfd, fds, LENGTH(fds), &magic_header, sizeof(magic_header)) == -1) {
    velvet_fatal("send mmap:");
  }
  /* the server owns the mmap now, so we can close it */
  velvet_alloc_shmem_destroy(shmem, shmem_fd);
  close(out_fd[1]);
  close(err_fd[1]);
  ensure_streams_blocking();
  color_stderr = isatty(STDERR_FILENO);

  struct velvet_lua_payload_context ctx = {0};
  struct io io = io_default;
  struct io_source out_src = {
    .fd = out_fd[0],
    .events = IO_SOURCE_POLLIN,
    .on_read = vv_lua_on_output,
  };
  io_add_source(&io, out_src);
  struct io_source err_src = {
    .fd = err_fd[0],
    .events = IO_SOURCE_POLLIN,
    .on_read = vv_lua_on_error,
  };
  io_add_source(&io, err_src);
  struct io_source socket_src = {
    .data = &ctx,
    .fd = sockfd,
    .events = IO_SOURCE_POLLIN,
    .on_read = vv_lua_on_socket,
  };
  io_add_source(&io, socket_src);
  if (isatty(STDIN_FILENO)) {
    struct io_source in_src = {
        .fd = STDIN_FILENO,
        .events = IO_SOURCE_POLLIN,
        .on_read = vv_lua_on_input,
        .data = &ctx,
    };
    io_add_source(&io, in_src);
  }
  while (!ctx.quit) {
    io_dispatch(&io);
    if (ctx.stdin_closed) {
      /* STDIN is the last entry */
      vec_remove_at(&io.sources, io.sources.length - 1);
    }
  }

  io_destroy(&io);
  restore_streams();
  close(sockfd);
  close(out_fd[0]);
  close(err_fd[0]);
  return ctx.exit_code;
}

static void string_read_fd(int fd, struct string *s) {
  char buf[4096];
  ssize_t n;
  while ((n = read(fd, buf, sizeof(buf))) > 0) {
    string_push_slice(s, (struct u8_slice){.content = (uint8_t *)buf, .len = (size_t)n});
  }
  if (string_starts_with_cstr(s, "#!")) {
    /* if the file starts with a shebang, comment it. Otherwise lua loadbuffer() will reject it. */
    s->content[0] = '-';
    s->content[1] = '-';
  }
  string_ensure_null_terminated(s);
}

static int vv_send_lua_chunk(struct velvet_args args) {
  struct string stdin_buf = {0};
  if (args.lua && strcmp(args.lua, "-") != 0) {
    /* this file is only available here -- probably provided in the shell with pipe redirection or similar
     * Just read the content instead. */
    int fd = open(args.lua, O_RDONLY);
    string_read_fd(fd, &stdin_buf);
    close(fd);
  } else {
    string_read_fd(STDIN_FILENO, &stdin_buf);
  }

  if (stdin_buf.len == 0) {
    fprintf(stderr, "Lua file is empty.\n");
    return -1;
  }

  int success = vv_send_lua_payload(args, string_as_u8_slice(stdin_buf));
  string_destroy(&stdin_buf);
  return success;
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
  if (str.len > 1 && str.content[0] == 'R') {
    /* hack: writing to the buffer is technically illegal,
     * but it's guaranteed to be large enough and we are about to exec(). */
    char *socket = (char*)str.content;
    socket[str.len] = 0;

    struct string pathbuf = {0};
    string_joinpath(&pathbuf, getenv("HOME"), ".local", "share", "velvet", "sockets", socket + 1);
    string_ensure_null_terminated(&pathbuf);

    terminal_light_reset();
    unsetenv("VELVET");

    if (file_is_socket((char*)pathbuf.content)) {
      execlp(main_argv[0], main_argv[0], "attach", "-S", socket + 1, NULL);
    } else {
      execlp(main_argv[0], main_argv[0], "-S", socket + 1, NULL);
    }

    velvet_die("execl:");
    /* reattach */
  }
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
static int stderr_flags = 0;
static void restore_streams(void) {
  fcntl(STDIN_FILENO, F_SETFL, stdin_flags);
  fcntl(STDOUT_FILENO, F_SETFL, stdout_flags);
  fcntl(STDERR_FILENO, F_SETFL, stderr_flags);
}

static void ensure_streams_blocking(void) {
  stdin_flags = fcntl(STDIN_FILENO, F_GETFL);
  if (stdin_flags == -1 || fcntl(STDIN_FILENO, F_SETFL, stdin_flags & ~O_NONBLOCK) == -1) {
    velvet_die("fcntl:");
  }

  stdout_flags = fcntl(STDOUT_FILENO, F_GETFL);
  if (stdout_flags == -1 || fcntl(STDOUT_FILENO, F_SETFL, stdout_flags & ~O_NONBLOCK) == -1) {
    velvet_die("fcntl:");
  }

  stderr_flags = fcntl(STDERR_FILENO, F_GETFL);
  if (stderr_flags == -1 || fcntl(STDERR_FILENO, F_SETFL, stderr_flags & ~O_NONBLOCK) == -1) {
    velvet_die("fcntl:");
  }
}

static void pipe_cloexec(int fds[2]) {
  if (pipe(fds) < 0) velvet_die("pipe:");
  set_cloexec(fds[0]);
  set_cloexec(fds[1]);
}

static void vv_attach(struct velvet_args args) {
  int signal_pipes[2];
  pipe_cloexec(signal_pipes);
  signal_write = signal_pipes[1];
  int signal_read = signal_pipes[0];

  struct sigaction sa = {0};
  sa.sa_sigaction = &attach_sighandler;
  sa.sa_flags = SA_RESTART;

  if (sigaction(SIGWINCH, &sa, NULL) == -1) velvet_die("sigaction:");

  struct rect ws;
  platform_get_winsize(&ws);

  terminal_setup(restore_streams);

  /* client logic depends on stdout being blocking for clean writes. */
  ensure_streams_blocking();

  int sockfd = vv_connect(args.socket);

  int output_pipe[2];
  pipe_cloexec(output_pipe);
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
