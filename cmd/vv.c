#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "utils.h"
#include "velvet.h"

static int signal_write;
static void signal_handler(int sig, siginfo_t *siginfo, void *context) {
  (void)siginfo, (void)context;
  ssize_t written = write(signal_write, &sig, sizeof(sig));
  if (written < (int)sizeof(sig)) velvet_die("signal write:");
}

static void install_signal_handlers(int *pipes) {
  struct sigaction sa = {0};
  sa.sa_sigaction = &signal_handler;
  sa.sa_flags = SA_SIGINFO;

  if (sigaction(SIGTERM, &sa, NULL) == -1) velvet_die("sigaction:");
  if (sigaction(SIGQUIT, &sa, NULL) == -1) velvet_die("sigaction:");
  if (sigaction(SIGINT, &sa, NULL) == -1) velvet_die("sigaction:");
  if (sigaction(SIGCHLD, &sa, NULL) == -1) velvet_die("sigaction:");
  if (sigaction(SIGHUP, &sa, NULL) == -1) velvet_die("sigaction:");
  if (sigaction(SIGPIPE, &sa, NULL) == -1) velvet_die("sigaction:");
  if (sigaction(SIGUSR1, &sa, NULL) == -1) velvet_die("sigaction:");
  if (sigaction(SIGUSR2, &sa, NULL) == -1) velvet_die("sigaction:");

  signal(SIGTTOU, SIG_IGN);
  signal(SIGTTIN, SIG_IGN);
  signal(SIGTSTP, SIG_IGN);

  if (pipe(pipes) < 0) velvet_die("pipe:");
}

static void add_bindir_to_path(void) {
  char *exe_path = platform_get_exe_path();
  char *path_var = getenv("PATH");
  if (!path_var) return;

  struct string new_path = {0};

  bool is_abs = *exe_path == '/';

  if (!is_abs) {
    char bindir[1024] = {0};
    getcwd(bindir, sizeof(bindir) - 1);
    string_push(&new_path, (uint8_t *)bindir);
    string_push_char(&new_path, '/');
  }

  char *last_slash = strrchr(exe_path, '/');
  if (last_slash) string_push_range(&new_path, (uint8_t *)exe_path, (size_t)(last_slash - exe_path));

  string_push_char(&new_path, ':');
  string_push(&new_path, (uint8_t *)path_var);
  string_push_char(&new_path, 0);
  setenv("PATH", (char *)new_path.content, true);
  string_destroy(&new_path);
  free(exe_path);
}

#define SOCKET_PATH_MAX ((sizeof((struct sockaddr_un*)((void*)0))->sun_path) - 1)

static int create_socket(char *path) {
  struct sockaddr_un addr = {.sun_family = AF_UNIX};
  bool success = false;
  int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sockfd == -1) velvet_die("socket:");

  if (!path) {
    char *base = "/tmp/velvet_sock";
    struct stat buf;
    for (int i = 1; i < 100 && !success; i++) {
      snprintf(addr.sun_path, sizeof(addr.sun_path) - 1, "%s.%d", base, i);
      success = stat(addr.sun_path, &buf) == -1;
    }
  } else {
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

static int get_flag(int argc, char **argv, char *flag) {
  for (int i = 1; i < argc; i++)
    if (strcmp(argv[i], flag) == 0) return i;
  return 0;
}

struct velvet_args {
  bool attach;
  bool foreground;
  char *socket;
  struct {
    char *keys;
    char *action;
  } bind;
  struct {
    char *option;
    char *value;
  } set;
  char *source;
};

static void vv_configure(struct velvet_args args);
static void vv_attach(struct velvet_args args);

static void usage(char *arg0) {
  printf("Usage:\n  %s [<options>] [<arguments>]\n\nOptions:\n"
         "  --attach                Attach to the server at <socket> if present.\n"
         "  --bind <keys> <action>  Bind <keys> to the action <action>.\n"
         "  --foreground            Start a server as a foreground process.\n"
         "  --source -              Read newline separated mapings from stdin\n"
         "  --source <FILE>         Read newline-separated mappings from a file\n"
         "  -S, --socket <socket>   Specify the socket to use instead of guessing or auto-generating it.\n"
         "  --set <option> <value>  Set the <option> to <value>.\n"
         "  -h, --help              Show this help text and exit.\n"
         , arg0);
}


static bool file_exists(const char *path) {
  struct stat st;
  return stat(path, &st) == 0;
}

static bool file_is_socket(const char *path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISSOCK(st.st_mode);
}

struct velvet_args velvet_parse_args(int argc, char **argv) {
  #define F(name) (strcmp(arg, #name) == 0)
  #define NEXT() ((++i) < argc ? argv[i] : nullptr)
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
    } else if (F(--set)) {
      n_commands++;
      if (a.set.option) velvet_fatal("--set specified multiple times");
      GET(a.set.option);
      GET(a.set.value);
    } else if (F(--foreground)) {
      n_commands++;
      if (a.foreground) velvet_fatal("--foreground specified multiple times.");
      a.foreground = true;
    } else if (F(--attach)) {
      if (nested) velvet_fatal("Nesting velvet sessions is not supported.");
      n_commands++;
      if (a.attach) velvet_fatal("--attach specified multiple times.");
      a.attach = true;
    } else if (F(--bind)) {
      n_commands++;
      if (a.bind.keys) velvet_fatal("--bind specified multiple times.");
      GET(a.bind.keys);
      GET(a.bind.action);
    } else if (F(--source)) {
      if (a.source) velvet_fatal("--source specified multiple times.");
      GET(a.source);
    } else {
      fprintf(stderr, "Unrecognized argument '%s'\n\n", arg);
      usage(argv[0]);
      exit(1);
    }
  }

  if (n_commands > 1) velvet_fatal("Multiple commands specified.");

  if ((a.bind.keys || a.source || a.set.option)) {
    if (!a.socket) a.socket = getenv("VELVET");
    if (!a.socket) velvet_fatal("Unable to map keys; Either specify the --socket or set $VELVET to a socket path.");
  }

  if (a.socket && !file_is_socket(a.socket) && (a.bind.keys || a.attach || a.source)) {
    velvet_fatal("Socket '%s' is not a unix domain socket.", a.socket);
  }

  if (a.socket && strlen(a.socket) > SOCKET_PATH_MAX) {
    velvet_fatal("Socket path max length exceeded. Max: %d", SOCKET_PATH_MAX);
  }

  return a;
}


int main(int argc, char **argv) {
  struct velvet_args args = velvet_parse_args(argc, argv);

  if (args.attach) {
    if (getenv("VELVET")) {
      velvet_fatal("Unable to attach; terminal is already in a velvet session.");
      return 1;
    }
    vv_attach(args);
    return 0;
  }

  if (args.bind.action || args.set.option) {
    vv_configure(args);
    return 0;
  }

  int sock_fd = create_socket(args.socket);

  // Since we are not connecting to a server, that means we are creating a new server.
  // The server should be detached from the current process hierarchy.
  // We do this with a classic double fork()
  if (!args.foreground) {
    struct platform_winsize ws = {0};
    platform_get_winsize(&ws);
    if (ws.rows == 0 || ws.colums == 0) {
      fprintf(stderr, "Error getting terminal size. Exiting.\n");
      return 1;
    }

    /* original process */
    if (fork()) {
      close(sock_fd);
      vv_attach(args);
      return 0;
    }

    // detach from controlling terminal
    if (!setsid()) velvet_die("setsid:");

    /* parent of child */
    if (fork()) {
      exit(0);
    }
  } else {
    printf("Server listening at %s\n", getenv("VELVET"));
  }

  add_bindir_to_path();
  int signal_pipes[2];
  install_signal_handlers(signal_pipes);
  signal_write = signal_pipes[1];
  // detached child process of exited parent

  if (!args.foreground) {
    close(STDIN_FILENO);
    // redirect all output/error to log files
    char *outpath = "/tmp/velvet.stdout";
    char *errpath = "/tmp/velvet.stderr";
    int new_stderr = open(errpath, O_TRUNC | O_CREAT | O_WRONLY, S_IRWXU);
    int new_stdout = open(outpath, O_TRUNC | O_CREAT | O_WRONLY, S_IRWXU);
    dup2(new_stderr, STDERR_FILENO);
    dup2(new_stdout, STDOUT_FILENO);
  }

  struct velvet velvet = {
      .scene = velvet_scene_default,
      .input = velvet_input_default,
      .sessions = vec(struct velvet_session),
      .socket = sock_fd,
      .event_loop = io_default,
      .signal_read = signal_pipes[0],
      .daemon = !args.foreground,
  };

  velvet_loop(&velvet);
  velvet_destroy(&velvet);
  return 0;
}

static void attach_sighandler(int sig, siginfo_t *siginfo, void *context) {
  (void)sig;
  (void)siginfo;
  (void)context;
}

static void vv_attach_send_message(int sockfd, struct platform_winsize ws, bool send_fds) {
  int fds[2] = {STDIN_FILENO, STDOUT_FILENO};
  char payload[100] = {0};
  int n_payload = snprintf(payload, 99, "\x1b[%d;%d;%d;%dW", ws.rows, ws.colums, ws.y_pixel, ws.x_pixel);
  char cmsgbuf[CMSG_SPACE(sizeof(fds))] = {0};

  struct msghdr msg = {.msg_iov = &(struct iovec){.iov_base = payload, .iov_len = n_payload}, .msg_iovlen = 1};

  if (send_fds) {
    msg.msg_control = cmsgbuf;
    msg.msg_controllen = sizeof(cmsgbuf);
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_len = CMSG_LEN(sizeof(fds));
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    memcpy(CMSG_DATA(cmsg), fds, sizeof(fds));
  }

  if (sendmsg(sockfd, &msg, 0) == -1) {
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

static void vv_configure(struct velvet_args args) {
  int sockfd = vv_connect(args.socket);
  char *word;
  char *first;
  char *second;
  if (args.bind.action) {
    word = "bind";
    first = args.bind.keys;
    second = args.bind.action;
  } else if (args.set.option) {
    word = "set";
    first = args.set.option;
    second = args.set.value;
  } else {
    velvet_fatal("Nothing to do.");
  }

  struct string payload = {0};
  string_push_format_slow(&payload, "%s %s \"%s\"", word, first, second);
  io_write(sockfd, string_as_u8_slice(&payload));

  char buf[1024] = {0};
  int n = read(sockfd, buf, sizeof(buf));
  if (n == -1) {
    velvet_fatal("read:");
  }
  printf("%.*s\n", n, buf);
  close(sockfd);
  string_destroy(&payload);
}

static void vv_attach(struct velvet_args args) {
  struct sigaction sa = {0};
  sa.sa_sigaction = &attach_sighandler;
  sa.sa_flags = SA_SIGINFO;

  if (sigaction(SIGWINCH, &sa, NULL) == -1) velvet_die("sigaction:");

  struct platform_winsize ws;
  platform_get_winsize(&ws);
  int sockfd = vv_connect(args.socket);

  terminal_setup();
  vv_attach_send_message(sockfd, ws, true);

  // Block until EOF on the socket
  char buf[32];

  bool detach = false;
  bool quit = false;
  for (; !detach && !quit;) {
    struct pollfd pfd = {.fd = sockfd, .events = POLLIN};
    int n = poll(&pfd, 1, -1);
    if (n == -1) {
      if (errno == EINTR) {
        struct platform_winsize ws2 = {0};
        platform_get_winsize(&ws2);
        if (ws2.colums && ws2.rows && (ws2.colums != ws.colums || ws2.rows != ws.rows)) {
          ws = ws2;
          vv_attach_send_message(sockfd, ws, false);
        }
      }
    }
    if (n > 0) {
      n = read(sockfd, buf, sizeof(buf));
      if (n == 0) {
        quit = true;
      } else if (n == 1 && buf[0] == 'D') {
        detach = true;
      }
    }
  }

  close(sockfd);
  terminal_reset();

  if (detach) {
    printf("[Detached]\n");
  } else {
    printf("[Shutdown]\n");
  }
}
