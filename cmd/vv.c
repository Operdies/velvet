#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/fcntl.h>
#include <utils.h>
#include <vte_host.h>
#include <collections.h>
#include <poll.h>
#include <io.h>
#include <multiplexer.h>
#include "csi.h"
#include "platform.h"
#include "velvet_input.h"
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

static int signal_write;
static int signal_read;
static void signal_handler(int sig, siginfo_t *siginfo, void *context) {
  (void)siginfo, (void)context;
  ssize_t written = write(signal_write, &sig, sizeof(sig));
  if (written < (int)sizeof(sig)) die("signal write:");
}

static void install_signal_handlers(void) {
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

  int pipes[2];
  if (pipe(pipes) < 0) {
    die("pipe:");
  }
  signal_read = pipes[0];
  signal_write = pipes[1];
}

struct session {
  int socket;                   // socket connection
  struct string pending_output; // buffered output
  int input;                    // stdin
  int output;                   // stdout
  struct platform_winsize ws;
};

struct app_context {
  struct multiplexer multiplexer;
  struct velvet_input input_handler;
  struct io event_loop;
  struct vec /* struct session */ sessions;
  /* this is modified by events such as receiving focus IN/OUT events, new sessions attaching, etc */
  size_t active_session;
  int socket;
  bool quit;
};

static void session_render(struct u8_slice str, void *context) {
  struct session *s = context;
  string_push_slice(&s->pending_output, str);
}

static void app_detach_session(struct app_context *app, struct session *s) {
  close(s->socket);
  close(s->input);
  close(s->output);
  string_destroy(&s->pending_output);
  size_t idx = vec_index(s, app->sessions);
  vec_remove(&app->sessions, idx);
  if (app->active_session >= idx) {
    app->active_session = MAX(app->active_session - 1, 0);
  }
}

static void session_socket_callback(struct io_source *src) {
  struct app_context *app = src->data;
  int sock = src->fd;
  struct msghdr msg = {0};
  struct iovec iov;
  char data_buf[256] = {0};
  int fds[2];
  char cmsgbuf[CMSG_SPACE(sizeof(fds))] = {0};

  // Normal data buffer
  iov.iov_base = data_buf;
  iov.iov_len = sizeof(data_buf);

  // Prepare message
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = cmsgbuf;
  msg.msg_controllen = sizeof(cmsgbuf);

  ssize_t n = recvmsg(sock, &msg, 0);
  if (n == -1) {
    ERROR("recvmsg:");
    return;
  }

  struct session *sesh;
  vec_find(sesh, app->sessions, sesh->socket == src->fd);

  if (n == 0) {
    if (sesh) app_detach_session(app, sesh);
    else close(sock);
  }

  assert(sesh);
  struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
  if (cmsg && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
    memcpy(fds, CMSG_DATA(cmsg), sizeof(fds));
    sesh->input = fds[0];
    sesh->output = fds[1];
    set_nonblocking(sesh->input);
    set_nonblocking(sesh->output);
    // Since we are normally only rendering lines which have changed,
    // new clients must receive a complete render upon connecting.
    multiplexer_render(&app->multiplexer, session_render, true, sesh);
    app->active_session = vec_index(sesh, app->sessions);
  }

  if (n > 2 && data_buf[0] == 0x1b && data_buf[1] == '[') {
    struct u8_slice client_size = {.content = (uint8_t *)data_buf + 2, .len = n - 2};
    struct csi c = {0};
    csi_parse(&c, client_size);

    if (c.state == CSI_ACCEPT) {
      if (c.final == 'W' && c.n_params == 4) {
        sesh->ws = (struct platform_winsize){
            .rows = c.params[0].primary,
            .colums = c.params[1].primary,
            .y_pixel = c.params[2].primary,
            .x_pixel = c.params[3].primary,
        };
        return;
      }
    }
  }

  TODO("Handle client message: %.*s", n, data_buf);
}

static void socket_connect_callback(struct io_source *src) {
  struct app_context *app = src->data;

  int client_fd = accept(src->fd, nullptr, nullptr);
  if (client_fd == -1) {
    ERROR("accept:");
  }

  struct session c = { .socket = client_fd };
  vec_push(&app->sessions, &c);
}

static bool redraw_needed = false;

static void signal_callback(struct io_source *src, struct u8_slice str) {
  struct app_context *app = src->data;
  // 1. Dispatch any pending signals
  bool did_sigchld = false;
  struct int_slice signals = { .content = (int*)str.content, .n = str.len / 4 };
  for (size_t i = 0; i < signals.n; i++) {
    int signal = signals.content[i];
    switch (signal) {
    case SIGTERM: {
      app->quit = true;
    } break;
    case SIGHUP: {
      logmsg("Ignoring SIGHUP");
    } break;
    case SIGCHLD: {
      did_sigchld = true;
    } break;
    default:
      app->quit = true;
      logmsg("Unhandled signal: %d", signal);
      break;
    }
  }

  if (did_sigchld) {
    multiplexer_remove_exited(&app->multiplexer);
    redraw_needed = true;
  }
}

static void session_input_callback(struct io_source *src, struct u8_slice str) {
  struct app_context *m = src->data;
  if (str.len == 0) {
    struct session *sesh;
    vec_find(sesh, m->sessions, sesh->input == src->fd);
    if (sesh) app_detach_session(m, sesh);
    return;
  }
  redraw_needed = true;
  velvet_input_process(&m->input_handler, str);
}

static ssize_t session_write_pending(struct session *sesh) {
  if (sesh->pending_output.len) {
    ssize_t written = io_write(sesh->output, string_as_u8_slice(&sesh->pending_output));
    logmsg("Write %zu / %zu\n", written, sesh->pending_output.len);
    if (written > 0) string_drop_left(&sesh->pending_output, (size_t)written);
    return written;
  }
  return 0;
}

static void app_render(struct u8_slice str, void *context) {
  struct app_context *a = context;
  struct session *sesh;
  if (str.len == 0) return;
  vec_foreach(sesh, a->sessions) {
    string_push_slice(&sesh->pending_output, str);
    session_write_pending(sesh);
  }
}

static void session_output_callback(struct io_source *src) {
  struct app_context *app = src->data;
  struct session *sesh;
  vec_find(sesh, app->sessions, sesh->output == src->fd);
  if (sesh && sesh->pending_output.len) {
    ssize_t written = session_write_pending(sesh);
    if (written == 0) {
      app_detach_session(app, sesh);
    }
  }
}

static void vte_write_callback(struct io_source *src) {
  struct vte_host *vte = src->data;
  if (vte->vte.pending_input.len) {
    ssize_t written = io_write(src->fd, string_as_u8_slice(&vte->vte.pending_input));
    if (written > 0) string_drop_left(&vte->vte.pending_input, (size_t)written);
  }
}

static void vte_read_callback(struct io_source *src, struct u8_slice str) {
  struct vte_host *vte = src->data;
  vte_host_process_output(vte, str);
  redraw_needed = true;
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
  if (last_slash)
    string_push_range(&new_path, (uint8_t*)exe_path, (size_t)(last_slash - exe_path));

  string_push_char(&new_path, ':');
  string_push(&new_path, (uint8_t*)path_var);
  string_push_char(&new_path, 0);
  setenv("PATH", (char*)new_path.content, true);
  string_destroy(&new_path);
  free(exe_path);
}

static void app_destroy(struct app_context *app) {
  io_destroy(&app->event_loop);
  multiplexer_destroy(&app->multiplexer);
  velvet_input_destroy(&app->input_handler);
  vec_destroy(&app->sessions);
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

static void draw_no_mans_land(struct app_context *app) {
  static struct string scratch = {0};
  struct session *active = vec_nth(&app->sessions, app->active_session);
  struct session *sesh;
  char *pipe = "│";
  char *dash = "─";
  char *corner = "┘";
  vec_foreach(sesh, app->sessions) {
    if (sesh->ws.colums && sesh->ws.rows) {
      string_clear(&scratch);
      string_push_csi(&scratch, 0, INT_SLICE(38, 2, 0x5e, 0x5e, 0x6e), "m");
      // 1. Draw the empty space to the right of this client
      if (sesh->ws.colums > active->ws.colums) {
        for (int i = 0; i < active->ws.rows; i++) {
          string_push_csi(&scratch, 0, INT_SLICE(i + 1, active->ws.colums + 1), "H");
          int draw_count = sesh->ws.colums - active->ws.colums;
          string_push_slice(&scratch, u8_slice_from_cstr(pipe));
          if (--draw_count) string_push_slice(&scratch, u8_slice_from_cstr("·"));
          if (--draw_count) string_push_csi(&scratch, 0, INT_SLICE(draw_count), "b");
        }
      }
      // 2. Draw the empty space below this client
      for (int i = active->ws.rows; i < sesh->ws.rows; i++) {
        int draw_count = sesh->ws.colums;
        string_push_csi(&scratch, 0, INT_SLICE(i + 1, 1), "H");
        if (i == active->ws.rows) {
          string_push_slice(&scratch, u8_slice_from_cstr(dash));
          string_push_csi(&scratch, 0, INT_SLICE(active->ws.colums - 1), "b");
          string_push_slice(&scratch, u8_slice_from_cstr(corner));
          draw_count = draw_count - active->ws.colums - 1;
        }
        if (--draw_count) string_push_slice(&scratch, u8_slice_from_cstr("·"));
        if (--draw_count) string_push_csi(&scratch, 0, INT_SLICE(draw_count), "b");
      }
      string_push_csi(&scratch, 0, INT_SLICE(0), "m");
      string_push_slice(&sesh->pending_output, string_as_u8_slice(&scratch));
    }
  }
}

static void start_server(struct app_context *app) {
  // Set an initial dummy size. This will be controlled by clients once they connect.
  struct platform_winsize ws = {.colums = 80, .rows = 24, .x_pixel = 800, .y_pixel = 240};

  struct io *const loop = &app->event_loop;

  multiplexer_resize(&app->multiplexer, ws);
  multiplexer_spawn_process(&app->multiplexer, "zsh");
  multiplexer_arrange(&app->multiplexer);

  for (;;) {
    logmsg("Main loop"); // mostly here to detect misbehaving polls.
    bool did_resize = false;
    if (app->sessions.length && app->active_session < app->sessions.length) {
      struct session *active = vec_nth(&app->sessions, app->active_session);
      if (active->ws.colums && active->ws.rows && (active->ws.colums != app->multiplexer.ws.colums || active->ws.rows != app->multiplexer.ws.rows)) {
        multiplexer_resize(&app->multiplexer, active->ws);
        did_resize = true;
        redraw_needed = true;
      }
    }

    // arrange
    if (redraw_needed) {
      redraw_needed = false;
      multiplexer_arrange(&app->multiplexer);
      if (did_resize) {
        draw_no_mans_land(app);
      }
      // Render the current app state
      multiplexer_render(&app->multiplexer, app_render, false, app);
    }

    // Set up IO
    vec_clear(&loop->sources);

    struct io_source signal_src = { .fd = signal_read, .events = IO_SOURCE_POLLIN, .read_callback = signal_callback, .data = app};
    /* NOTE: the 'h' pointer is only guaranteed to be valid until signals and stdin are processed.
     * This is because the signal handler will remove closed clients, and the stdin handler
     * processes hotkeys which can rearrange the order of the pointers.
     * */
    struct vte_host *h;
    vec_foreach(h, app->multiplexer.hosts) {
      struct io_source read_src = {
        .data = h,
        .fd = h->pty,
        .events = IO_SOURCE_POLLIN,
        .read_callback = vte_read_callback,
        .write_callback = vte_write_callback,
      };
      if (h->vte.pending_input.len) read_src.events |= IO_SOURCE_POLLOUT;

      io_add_source(loop, read_src);
    }

    io_add_source(loop, signal_src);

    struct io_source socket_src = { .fd = app->socket, .events = IO_SOURCE_POLLIN, .ready_callback = socket_connect_callback, .data = app };
    io_add_source(loop, socket_src);

    struct session *session;
    vec_foreach(session, app->sessions) {
      struct io_source socket_src = { .fd = session->socket, .events = IO_SOURCE_POLLIN, .ready_callback = session_socket_callback, .data = app };
      io_add_source(loop, socket_src);
      struct io_source input_src = { .fd = session->input, .events = IO_SOURCE_POLLIN, .read_callback = session_input_callback, .data = app};
      io_add_source(loop, input_src);
      if (session->pending_output.len) {
        struct io_source output_src = { .fd = session->output, .events = IO_SOURCE_POLLOUT, .write_callback = session_output_callback, .data = app};
        io_add_source(loop, output_src);
      }
    }

    // Dispatch all pending io
    io_dispatch(loop);

    // quit ?
    if (app->multiplexer.hosts.length == 0 || app->quit) break;
  }

  close(app->socket);
  char *sockpath = getenv("VELVET");
  if (sockpath) {
    unlink(sockpath);
  }
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

    if (fork()) {
      // original process
      close(sock_fd);
      vv_attach(getenv("VELVET"));
      return 0;
    }

    if (fork()) {
      // new child. exiting this process causes the server process to be detached
      return 0;
    }
  }

  add_bindir_to_path();
  install_signal_handlers();
  // detached child process of exited parent

  // redirect all input/output and close handles.
  char *outpath = "/tmp/velvet.stdout";
  char *errpath = "/tmp/velvet.stderr";
  int new_stderr = open(errpath, O_TRUNC | O_CREAT | O_WRONLY, S_IRWXU);
  int new_stdout = open(outpath, O_TRUNC | O_CREAT | O_WRONLY, S_IRWXU);
  dup2(new_stderr, STDERR_FILENO);
  dup2(new_stdout, STDOUT_FILENO);
  close(STDIN_FILENO);

  struct app_context app = {
      .multiplexer = multiplexer_default,
      .input_handler = (struct velvet_input){.m = &app.multiplexer, .options = {.focus_follows_mouse = true}},
      .sessions = vec(struct session),
      .socket = sock_fd,
      .event_loop = io_default,
  };

  start_server(&app);
  app_destroy(&app);
  return 0;
}

static void attach_sighandler(int sig, siginfo_t *siginfo, void *context) {
  (void)sig;(void)siginfo;(void)context;
}

static void vv_attach_send_message(int sockfd, struct platform_winsize ws, bool send_fds) {
  struct msghdr msg = {0};
  struct iovec iov = {0};

  int fds[2] = {STDIN_FILENO, STDOUT_FILENO};
  char cmsgbuf[CMSG_SPACE(sizeof(fds))] = {0};

  char payload[100] = {0};
  int n_payload = snprintf(payload, 99, "\x1b[%d;%d;%d;%dW", ws.rows, ws.colums, ws.y_pixel, ws.x_pixel);
  iov.iov_base = payload;
  iov.iov_len = n_payload;
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;

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
      die("No server to attach to.");
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
