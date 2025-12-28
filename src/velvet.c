#include "velvet.h"
#include "utils.h"
#include <stdlib.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <csi.h>
#include <string.h>
#include "velvet_cmd.h"
#include <signal.h>
#include <sys/wait.h>
#include <pwd.h>

static void velvet_session_render(struct u8_slice str, void *context) {
  struct velvet_session *s = context;
  string_push_slice(&s->pending_output, str);
}

void velvet_detach_session(struct velvet *velvet, struct velvet_session *s) {
  if (!s) return;
  int sock = s->socket;
  if (s->socket) {
    uint8_t detach = 'D';
    write(s->socket, &detach, 1);
    close(s->socket);
  }
  if (s->input)
    close(s->input);
  if (s->output)
    close(s->output);
  string_destroy(&s->pending_output);
  string_destroy(&s->commands.buffer);
  *s = (struct velvet_session){0};
  size_t idx = vec_index(&velvet->sessions, s);
  vec_remove_at(&velvet->sessions, idx);
  if (sock && velvet->focused_socket == sock) {
    struct velvet_session *fst = nullptr;
    vec_find(fst, velvet->sessions, fst->socket && fst->input);
    velvet->focused_socket = fst ? fst->socket : 0;
  }
}

struct velvet_session *velvet_get_focused_session(struct velvet *v) {
  if (v->sessions.length && v->focused_socket) {
    struct velvet_session *f;
    vec_find(f, v->sessions, f->socket == v->focused_socket);
    return f;
  }
  return nullptr;
}

static void session_handle_command_buffer(struct velvet *v, struct velvet_session *src) {
  int socket = src->socket;
  struct velvet_cmd_iterator it = {.src = string_as_u8_slice(src->commands.buffer)};

  /* if the command is from an open socket, we can't know if the last line in the input
   * is complete or partial. This final line will only be handled once it is either terminated,
   * or the socket is closed.
   */
  bool require_newline = src->socket != 0;
  it.require_terminator = require_newline;
  while (velvet_cmd_iterator_next(&it)) {
    velvet_cmd(v, socket, it.current);
  }

  /* the command buffer may contain a partial command.
   * Drop all the commands we have actually handled and buffer the partial command for later.
   */
  if (it.cursor) {
    string_shift_left(&src->commands.buffer, it.cursor);
    src->commands.lines += it.line_count;
  }
}

static void session_socket_callback(struct io_source *src) {
  struct velvet *velvet = src->data;
  char data_buf[2048] = {0};
  int fds[2] = {0};
  char cmsgbuf[CMSG_SPACE(sizeof(fds))] = {0};
  struct msghdr msg = {
      .msg_iov = &(struct iovec){.iov_base = data_buf, .iov_len = sizeof(data_buf)},
      .msg_iovlen = 1,
      .msg_control = cmsgbuf,
      .msg_controllen = sizeof(cmsgbuf),
  };

  ssize_t n = recvmsg(src->fd, &msg, 0);
  if (n == -1) {
    ERROR("recvmsg:");
    return;
  }

  struct velvet_session *session;
  vec_find(session, velvet->sessions, session->socket == src->fd);
  assert(session);

  if (n == 0) {
    // The socket was closed, so let's ensure we don't write to it or close it again
    close(session->socket);
    session->socket = 0;
    session_handle_command_buffer(velvet, session);
    velvet_detach_session(velvet, session);
    return;
  }

  assert(session);

  bool needs_render = false;
  struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
  if (cmsg && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
    memcpy(fds, CMSG_DATA(cmsg), sizeof(fds));
    session->input = fds[0];
    session->output = fds[1];

    set_nonblocking(session->output);

    // Since we are normally only rendering lines which have changed,
    // new clients must receive a complete render upon connecting.
    velvet->focused_socket = session->socket;
    needs_render = true;
  }

  struct u8_slice cmd = {.content = (uint8_t *)data_buf, .len = n};
  string_push_slice(&session->commands.buffer, cmd);
  session_handle_command_buffer(velvet, session);

  if (needs_render) {
    velvet_scene_render_full(&velvet->scene, velvet_session_render, session);
  }
}

static void socket_accept(struct io_source *src) {
  struct velvet *velvet = src->data;

  int client_fd = accept(src->fd, nullptr, nullptr);
  if (client_fd == -1) {
    ERROR("accept:");
  }

  struct velvet_session c = { .socket = client_fd };
  vec_push(&velvet->sessions, &c);
}

static void velvet_remove_window(struct velvet *v, struct velvet_window *h) {
  struct velvet_scene *m = &v->scene;
  h->pid = 0;
  velvet_window_destroy(h);
  velvet_scene_remove_window(m, h);
}

static void velvet_scene_remove_schedule(void *data) {
  struct velvet *v = data;
  uint64_t now = get_ms_since_startup();
  struct velvet_window *h;
  vec_rwhere(h,
             v->scene.windows,
             (h->close.when & VELVET_WINDOW_CLOSE_AFTER_DELAY) && h->exited_at &&
                 h->exited_at + h->close.delay_ms <= now) {
    velvet_remove_window(v, h);
  }
}

void velvet_scene_remove_exited(struct velvet *v) {
  int status;
  pid_t pid = 0;
  struct velvet_scene *m = &v->scene;

  while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
    struct velvet_window *h;
    vec_find(h, m->windows, h->pid == pid);
    if (h) {
      h->exited_at = get_ms_since_startup();
      if (h->close.when & VELVET_WINDOW_CLOSE_AFTER_DELAY && h->close.delay_ms > 0) {
        io_schedule(&v->event_loop, h->close.delay_ms, velvet_scene_remove_schedule, v);
      } else {
        velvet_remove_window(v, h);
      }
    }
  }
}

static void on_signal(struct io_source *src, struct u8_slice str) {
  struct velvet *velvet = src->data;
  // 1. Dispatch any pending signals
  bool did_sigchld = false;
  struct int_slice signals = {.content = (int *)str.content, .n = str.len / 4};
  for (size_t i = 0; i < signals.n; i++) {
    int signal = signals.content[i];
    switch (signal) {
    case SIGTERM: {
      velvet->quit = true;
    } break;
    case SIGHUP: {
      velvet_log("Ignoring SIGHUP");
    } break;
    case SIGCHLD: {
      did_sigchld = true;
    } break;
    case SIGINT: {
      if (!velvet->daemon) {
        velvet_log("^C received; exiting");
        velvet->quit = true;
      }
    } break;
    default:
      velvet->quit = true;
      velvet_die("Unhandled signal: %d", signal);
      break;
    }
  }

  if (did_sigchld) {
    velvet_scene_remove_exited(velvet);
  }
}

static ssize_t session_write_pending(struct velvet_session *sesh) {
  assert(sesh->input);
  assert(sesh->output);
  if (sesh->pending_output.len) {
    struct u8_slice pending = string_as_u8_slice(sesh->pending_output);
    ssize_t written = io_write(sesh->output, pending);
    velvet_log("Write %zu / %zu", written, sesh->pending_output.len);
    if (written > 0) string_shift_left(&sesh->pending_output, (size_t)written);
    return written;
  }
  return -1;
}

static void velvet_render(struct u8_slice str, void *context) {
  struct velvet *a = context;
  struct velvet_session *s;
  if (str.len == 0) return;

  vec_where(s, a->sessions, s->output) string_push_slice(&s->pending_output, str);
  vec_where(s, a->sessions, s->output) session_write_pending(s);
}

static void on_session_input(struct io_source *src, struct u8_slice str) {
  struct velvet *m = src->data;
  struct velvet_session *session;
  vec_find(session, m->sessions, session->input == src->fd);

  if (str.len == 0) {
    velvet_detach_session(m, session);
    return;
  }

  if (session) m->input.input_socket = session->socket;
  velvet_input_process(m, str);
  m->input.input_socket = 0;
}

static void on_session_writable(struct io_source *src) {
  struct velvet *velvet = src->data;
  struct velvet_session *sesh;
  vec_find(sesh, velvet->sessions, sesh->output == src->fd);
  if (sesh && sesh->pending_output.len) {
    ssize_t written = session_write_pending(sesh);
    if (written == 0) {
      velvet_detach_session(velvet, sesh);
    }
  }
}

static void on_window_writable(struct io_source *src) {
  struct velvet *v = src->data;
  struct velvet_window *vte;
  vec_find(vte, v->scene.windows, vte->pty == src->fd);
  assert(vte);
  if (vte->emulator.pending_input.len) {
    ssize_t written = io_write(src->fd, string_as_u8_slice(vte->emulator.pending_input));
    if (written > 0) string_shift_left(&vte->emulator.pending_input, (size_t)written);
  }
}

static void on_window_output(struct io_source *src, struct u8_slice str) {
  struct velvet *v = src->data;
  struct velvet_window *vte;
  vec_find(vte, v->scene.windows, vte->pty == src->fd);
  assert(vte);
  velvet_window_process_output(vte, str);
}

static void velvet_default_config(struct velvet *v) {
  char *config = "map <C-x>c 'spawn zsh'\n"
                 "map <C-x>d detach\n"
                 "map <C-x>t set layer tiled\n"
                 "map <C-x>f set layer floating\n"
                 "map -r <C-x>b spawn bash\n"
                 "map -r <C-x>x incborder\n"
                 "map -r <C-x>a decborder\n"
                 "map -r <C-x>[ decfactor\n"
                 "map -r <C-x>] incfactor\n"
                 "map -r <C-x>i incnmaster\n"
                 "map -r <C-x>o decnmaster\n"
                 "map <C-x><C-x> put <C-x>\n"
                 "map <C-x>,,, set display_damage true\n"
                 "map <C-x>... set display_damage false\n"
                 "map -r '<C-x><C-j>' focus-next\n"
                 "map -r '<C-x><C-k>' focus-previous\n"
                 "map -r '<C-x>j' swap-next\n"
                 "map -r '<C-x>k' swap-previous\n";

  struct u8_slice cfg = u8_slice_from_cstr(config);
  struct velvet_cmd_iterator it = {.src = cfg};
  while (velvet_cmd_iterator_next(&it)) {
    velvet_cmd(v, 0, it.current);
  }
}


static void start_default_shell(struct velvet *v) {
  char *shell = nullptr;
  uid_t uid = getuid();
  struct passwd *pw = getpwuid(uid);

  if (pw == NULL) {
    velvet_log("Error getting default shell:");
    shell = "sh";
  } else {
    shell = pw->pw_shell;
  }
  velvet_scene_spawn_process(&v->scene, u8_slice_from_cstr(shell));
}

static void wakeup(void *) {}

void velvet_loop(struct velvet *velvet) {
  // Set an initial dummy size. This will be controlled by clients once they connect.
  struct rect ws = {.w = 80, .h = 24, .x_pixel = 800, .y_pixel = 600};
  struct io *const loop = &velvet->event_loop;

  {
    struct velvet_keymap *root = calloc(1, sizeof(*velvet->input.keymap));
    *root = (struct velvet_keymap){
        .root = root,
        .data = velvet,
        .on_key = velvet_input_send,
    };
    velvet->input.keymap = root;
  }

  velvet_scene_resize(&velvet->scene, ws);
  start_default_shell(velvet);
  velvet->scene.arrange(&velvet->scene);

  velvet_default_config(velvet);

  for (;;) {
    velvet_log("Main loop"); // mostly here to detect misbehaving polls.
    struct velvet_session *focus = velvet_get_focused_session(velvet);
    if (focus) {
      if (focus->ws.w && focus->ws.h && (focus->ws.w != velvet->scene.ws.w || focus->ws.h != velvet->scene.ws.h)) {
        velvet_scene_resize(&velvet->scene, focus->ws);
        velvet->scene.arrange(&velvet->scene);
        /* after resizing, we want to give clients a bit of time to respond before rendering.
         * They may not respond at all, so we schedule a wakeup a brief amount of time in the future */
        io_schedule(loop, 30, wakeup, nullptr);
      } else {
        velvet->scene.arrange(&velvet->scene);
        /* Render the current velvet state.
         * Note that we render *before* resizing in order to transmit the current state,
         * and then give clients an opportunity to adjust to the resize. */
        velvet->scene.renderer.options.no_repeat_wide_chars = focus->features.no_repeat_wide_chars;
        velvet_scene_render_damage(&velvet->scene, velvet_render, velvet);
      }
    }

    // Set up IO
    vec_clear(&loop->sources);

    struct io_source signal_src = { .fd = velvet->signal_read, .events = IO_SOURCE_POLLIN, .on_read = on_signal, .data = velvet};
    /* NOTE: the 'h' pointer is only guaranteed to be valid until signals and stdin are processed.
     * This is because the signal handler will remove closed clients, and the stdin handler
     * processes hotkeys which can rearrange the order of the pointers.
     * */
    struct velvet_window *h;
    vec_foreach(h, velvet->scene.windows) {
      struct io_source read_src = {
        .data = velvet,
        .fd = h->pty,
        .events = IO_SOURCE_POLLIN,
        .on_read = on_window_output,
        .on_writable = on_window_writable,
      };
      if (h->emulator.pending_input.len) read_src.events |= IO_SOURCE_POLLOUT;

      io_add_source(loop, read_src);
    }

    io_add_source(loop, signal_src);

    struct io_source socket_src = { .fd = velvet->socket, .events = IO_SOURCE_POLLIN, .on_readable = socket_accept, .data = velvet };
    io_add_source(loop, socket_src);

    struct velvet_session *session;
    vec_foreach(session, velvet->sessions) {
      struct io_source socket_src = { .fd = session->socket, .events = IO_SOURCE_POLLIN, .on_readable = session_socket_callback, .data = velvet };
      io_add_source(loop, socket_src);
      struct io_source input_src = { .fd = session->input, .events = IO_SOURCE_POLLIN, .on_read = on_session_input, .data = velvet};
      if (input_src.fd)
        io_add_source(loop, input_src);
      if (session->pending_output.len) {
        struct io_source output_src = { .fd = session->output, .events = IO_SOURCE_POLLOUT, .on_writable = on_session_writable, .data = velvet};
        if (output_src.fd)
          io_add_source(loop, output_src);
      }
    }

    // Dispatch all pending io
    io_dispatch(loop);

    // quit ?
    struct velvet_window *real_client;
    vec_find(real_client,
             velvet->scene.windows,
             real_client->kind == VELVET_WINDOW_PTY_HOST && real_client->layer != VELVET_LAYER_NOTIFICATION &&
                 real_client->layer != VELVET_LAYER_BACKGROUND);

    if (!real_client || velvet->quit) break;
  }

  close(velvet->socket);
  char *sockpath = getenv("VELVET");
  if (sockpath) {
    unlink(sockpath);
  }
}

void velvet_destroy(struct velvet *velvet) {
  io_destroy(&velvet->event_loop);
  velvet_scene_destroy(&velvet->scene);
  velvet_input_destroy(&velvet->input);
  vec_destroy(&velvet->sessions);
}

