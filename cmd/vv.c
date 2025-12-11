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
  if (written < (int)sizeof(sig)) die("signal write:");
}

static void install_signal_handlers(int *pipes) {
  struct sigaction sa = {0};
  sa.sa_sigaction = &signal_handler;
  sa.sa_flags = SA_SIGINFO;
  if (sigaction(SIGTERM, &sa, NULL) == -1) {
    die("sigaction:");
  }
  if (sigaction(SIGINT, &sa, NULL) == -1) {
    die("sigaction:");
  }
  if (sigaction(SIGCHLD, &sa, NULL) == -1) {
    die("sigaction:");
  }
  if (sigaction(SIGHUP, &sa, NULL) == -1) {
    die("sigaction:");
  }

  if (pipe(pipes) < 0) {
    die("pipe:");
  }
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

static int create_socket() {
  int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sockfd == -1) die("socket:");
  char *base = "/tmp/velvet_sock";
  struct sockaddr_un addr = {.sun_family = AF_UNIX};
  struct stat buf;
  bool success = false;
  for (int i = 1; i < 100 && !success; i++) {
    snprintf(addr.sun_path, sizeof(addr.sun_path) - 1, "%s.%d", base, i);
    success = stat(addr.sun_path, &buf) == -1;
  }

  if (!success) die("No free socket.");
  if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
    close(sockfd);
    die("bind:");
  }

  if (listen(sockfd, 1) == -1) {
    unlink(addr.sun_path);
    close(sockfd);
    die("listen:");
  }

  setenv("VELVET", addr.sun_path, true);
  return sockfd;
}

static void vv_attach(char *SOCKET_PATH);

static int get_flag(int argc, char **argv, char *flag) {
  for (int i = 1; i < argc; i++)
    if (strcmp(argv[i], flag) == 0) return i;
  return 0;
}

int main(int argc, char **argv) {
#define FLAG(name) (get_flag(argc, argv, name))
#define ARGUMENT(name) (FLAG(name) && FLAG(name) < (argc - 1) ? argv[FLAG(name) + 1] : nullptr)

  char *attach = ARGUMENT("attach");
  if (attach || FLAG("attach")) {
    if (getenv("VELVET")) {
      fprintf(stderr, "Unable to attach; terminal is already in a velvet session.");
      return 1;
    }
    vv_attach(attach);
    return 0;
  }

  // in headless mode, the server starts with no GUI on
  bool headless = FLAG("--headless");

  int sock_fd = create_socket();

  // Since we are not connecting to a server, that means we are creating a new server.
  // The server should be detached from the current process hierarchy.
  // We do this with a classic double fork()
  if (!headless) {
    struct platform_winsize ws = {0};
    platform_get_winsize(&ws);
    if (ws.rows == 0 || ws.colums == 0) {
      fprintf(stderr, "Error getting terminal size. Exiting.\n");
      return 1;
    }

    /* original process */
    if (fork()) {
      close(sock_fd);
      vv_attach(getenv("VELVET"));
      return 0;
    }

    // detach from controlling terminal
    if (!setsid()) die("setsid:");

    /* parent of child */
    if (fork()) {
      exit(0);
    }
  }

  add_bindir_to_path();
  int signal_pipes[2];
  install_signal_handlers(signal_pipes);
  signal_write = signal_pipes[1];
  // detached child process of exited parent

  close(STDIN_FILENO);
  // redirect all output/error to log files
  char *outpath = "/tmp/velvet.stdout";
  char *errpath = "/tmp/velvet.stderr";
  int new_stderr = open(errpath, O_TRUNC | O_CREAT | O_WRONLY, S_IRWXU);
  int new_stdout = open(outpath, O_TRUNC | O_CREAT | O_WRONLY, S_IRWXU);
  dup2(new_stderr, STDERR_FILENO);
  dup2(new_stdout, STDOUT_FILENO);

  struct velvet velvet = {
      .scene = velvet_scene_default,
      .input_handler = (struct velvet_input){.m = &velvet.scene, .options = {.focus_follows_mouse = true}},
      .sessions = vec(struct velvet_session),
      .socket = sock_fd,
      .event_loop = io_default,
      .signal_read = signal_pipes[0],
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
    die("sendmsg:");
  }
}

static void vv_attach(char *vv_socket) {
  struct sigaction sa = {0};
  sa.sa_sigaction = &attach_sighandler;
  sa.sa_flags = SA_SIGINFO;

  if (sigaction(SIGWINCH, &sa, NULL) == -1) die("sigaction:");

  struct platform_winsize ws;
  platform_get_winsize(&ws);
  int sockfd;
  // Create the client socket
  sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sockfd == -1) {
    die("socket:");
  }

  struct sockaddr_un addr = {.sun_family = AF_UNIX};

  if (!vv_socket) {
    char *base = "/tmp/velvet_sock";
    struct stat buf;
    bool connected = false;
    for (int i = 1; i < 100 && !connected; i++) {
      snprintf(addr.sun_path, sizeof(addr.sun_path) - 1, "%s.%d", base, i);
      bool exists = stat(addr.sun_path, &buf) == 0;
      if (exists) {
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
      die("connect:");
    }
  }

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
