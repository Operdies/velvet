#include "velvet.h"
#include "utils.h"
#include <stdlib.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <csi.h>
#include <string.h>
#include "velvet_cmd.h"

static void velvet_session_render(struct u8_slice str, void *context) {
  struct velvet_session *s = context;
  string_push_slice(&s->pending_output, str);
}

void velvet_detach_session(struct velvet *velvet, struct velvet_session *s) {
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
    string_drop_left(&src->commands.buffer, it.cursor);
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

static void signal_callback(struct io_source *src, struct u8_slice str) {
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
    velvet_scene_remove_exited(&velvet->scene);
  }
}

static void draw_no_mans_land(struct velvet *velvet) {
  static struct string scratch = {0};
  struct velvet_session *focused = velvet_get_focused_session(velvet);
  struct velvet_session *s;
  char *pipe = "│";
  char *dash = "─";
  char *corner = "┘";
  vec_foreach(s, velvet->sessions) {
    if (s->ws.w && s->ws.h) {
      string_clear(&scratch);
      string_push_csi(&scratch, 0, INT_SLICE(38, 2, 0x5e, 0x5e, 0x6e), "m");
      // 1. Draw the empty space to the right of this client
      if (s->ws.w > focused->ws.w) {
        for (int i = 0; i < focused->ws.h; i++) {
          string_push_csi(&scratch, 0, INT_SLICE(i + 1, focused->ws.w + 1), "H");
          int draw_count = s->ws.w - focused->ws.w;
          string_push_slice(&scratch, u8_slice_from_cstr(pipe));
          if (--draw_count > 0) string_push_slice(&scratch, u8_slice_from_cstr("·"));
          if (--draw_count > 0) string_push_csi(&scratch, 0, INT_SLICE(draw_count), "b");
        }
      }
      // 2. Draw the empty space below this client
      for (int i = focused->ws.h; i < s->ws.h; i++) {
        int draw_count = s->ws.w;
        string_push_csi(&scratch, 0, INT_SLICE(i + 1, 1), "H");
        if (i == focused->ws.h) {
          string_push_slice(&scratch, u8_slice_from_cstr(dash));
          draw_count--;
          int n_dashes = MIN(draw_count, focused->ws.w - 1);
          string_push_csi(&scratch, 0, INT_SLICE(n_dashes), "b");
          draw_count -= n_dashes;
          if (draw_count > 0) string_push_slice(&scratch, u8_slice_from_cstr(corner));
          draw_count--;
        }
        if (draw_count > 0) string_push_slice(&scratch, u8_slice_from_cstr("·"));
        if (--draw_count > 0) string_push_csi(&scratch, 0, INT_SLICE(draw_count), "b");
      }
      string_push_csi(&scratch, 0, INT_SLICE(0), "m");
      string_push_slice(&s->pending_output, string_as_u8_slice(scratch));
    }
  }
}

static ssize_t session_write_pending(struct velvet_session *sesh) {
  assert(sesh->input);
  assert(sesh->output);
  if (sesh->pending_output.len) {
    struct u8_slice pending = string_as_u8_slice(sesh->pending_output);
    ssize_t written = io_write(sesh->output, pending);
    velvet_log("Write %zu / %zu", written, sesh->pending_output.len);
    if (written > 0) string_drop_left(&sesh->pending_output, (size_t)written);
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

static void session_input_callback(struct io_source *src, struct u8_slice str) {
  struct velvet *m = src->data;
  if (str.len == 0) {
    struct velvet_session *sesh;
    vec_find(sesh, m->sessions, sesh->input == src->fd);
    if (sesh) velvet_detach_session(m, sesh);
    return;
  }
  velvet_input_process(m, str);

  if (strncmp((char*)str.content, "\x1b[I", 3) == 0) {
    struct velvet_session *sesh;
    vec_find(sesh, m->sessions, sesh->input == src->fd);
    if (sesh) m->focused_socket = sesh->socket;
  }

  if (str.len == 2 && strncmp((char*)str.content, "\x1b]", 2) == 0) {
    struct velvet_session *sesh;
    vec_find(sesh, m->sessions, sesh->input == src->fd);
    if (sesh) velvet_detach_session(m, sesh);
  }
}

static void session_output_callback(struct io_source *src) {
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

static void on_pty_writable(struct io_source *src) {
  struct velvet *v = src->data;
  struct pty_host *vte;
  vec_find(vte, v->scene.hosts, vte->pty == src->fd);
  assert(vte);
  if (vte->emulator.pending_input.len) {
    ssize_t written = io_write(src->fd, string_as_u8_slice(vte->emulator.pending_input));
    if (written > 0) string_drop_left(&vte->emulator.pending_input, (size_t)written);
  }
}

static void on_pty_output(struct io_source *src, struct u8_slice str) {
  struct velvet *v = src->data;
  struct pty_host *vte;
  vec_find(vte, v->scene.hosts, vte->pty == src->fd);
  assert(vte);
  pty_host_process_output(vte, str);
}

static void velvet_default_config(struct velvet *v) {
  char *config = "map <C-x>c 'spawn zsh'\n"
                 "map <C-x>d detach\n"
                 "map <C-x>b spawn bash\n"
                 "map -r <C-x>j focus-next\n"
                 "map -r <C-x>k focus-previous\n"
                 "map -r <C-x>j swap-next\n"
                 "map -r <C-x>k swap-previous\n";

  struct u8_slice cfg = u8_slice_from_cstr(config);
  struct velvet_cmd_iterator it = {.src = cfg};
  while (velvet_cmd_iterator_next(&it)) {
    velvet_cmd(v, 0, it.current);
  }
}

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
  velvet_scene_spawn_process(&velvet->scene, u8_slice_from_cstr("zsh"));
  velvet_scene_spawn_process(&velvet->scene, u8_slice_from_cstr("bash"));
  velvet_scene_spawn_process(&velvet->scene, u8_slice_from_cstr("nvim"));
  velvet_scene_arrange(&velvet->scene);

  velvet_default_config(velvet);

  bool did_resize = false;
  for (;;) {
    velvet_log("Main loop"); // mostly here to detect misbehaving polls.
    struct velvet_session *focus = velvet_get_focused_session(velvet);
    if (focus) {
      if (focus->ws.w && focus->ws.h && (focus->ws.w != velvet->scene.ws.w || focus->ws.h != velvet->scene.ws.h)) {
        velvet_scene_resize(&velvet->scene, focus->ws);
        did_resize = true;
        // Defer redraw until the clients have actually updated. Redrawing right away leads to flickering
        // redraw_needed = true;
      }

      // arrange
      velvet_scene_arrange(&velvet->scene);
      // Render the current velvet state
      velvet->scene.renderer.options.no_repeat_wide_chars = focus->features.no_repeat_wide_chars;
      velvet_scene_render_damage(&velvet->scene, velvet_render, velvet);
      if (did_resize) {
        did_resize = false;
        draw_no_mans_land(velvet);
      }
    }

    // Set up IO
    vec_clear(&loop->sources);

    struct io_source signal_src = { .fd = velvet->signal_read, .events = IO_SOURCE_POLLIN, .on_read = signal_callback, .data = velvet};
    /* NOTE: the 'h' pointer is only guaranteed to be valid until signals and stdin are processed.
     * This is because the signal handler will remove closed clients, and the stdin handler
     * processes hotkeys which can rearrange the order of the pointers.
     * */
    struct pty_host *h;
    vec_foreach(h, velvet->scene.hosts) {
      struct io_source read_src = {
        .data = velvet,
        .fd = h->pty,
        .events = IO_SOURCE_POLLIN,
        .on_read = on_pty_output,
        .on_writable = on_pty_writable,
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
      struct io_source input_src = { .fd = session->input, .events = IO_SOURCE_POLLIN, .on_read = session_input_callback, .data = velvet};
      if (input_src.fd)
        io_add_source(loop, input_src);
      if (session->pending_output.len) {
        struct io_source output_src = { .fd = session->output, .events = IO_SOURCE_POLLOUT, .on_writable = session_output_callback, .data = velvet};
        if (output_src.fd)
          io_add_source(loop, output_src);
      }
    }

    // Dispatch all pending io
    io_dispatch(loop);

    // quit ?
    if (velvet->scene.hosts.length == 0 || velvet->quit) break;
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

