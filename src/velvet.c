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
  struct velvet_cmd_iterator it = {.src = string_as_u8_slice(&src->commands.buffer)};

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

  struct velvet_session *sesh;
  vec_find(sesh, velvet->sessions, sesh->socket == src->fd);
  assert(sesh);

  if (n == 0) {
    // The socket was closed, so let's ensure we don't write to it or close it again
    close(sesh->socket);
    sesh->socket = 0;
    session_handle_command_buffer(velvet, sesh);
    velvet_detach_session(velvet, sesh);
    return;
  }

  assert(sesh);

  bool needs_render = false;
  struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
  if (cmsg && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
    memcpy(fds, CMSG_DATA(cmsg), sizeof(fds));
    sesh->input = fds[0];
    sesh->output = fds[1];
    set_nonblocking(sesh->input);
    set_nonblocking(sesh->output);
    // Since we are normally only rendering lines which have changed,
    // new clients must receive a complete render upon connecting.
    velvet->focused_socket = sesh->socket;
    needs_render = true;
  }

  struct u8_slice cmd = {.content = (uint8_t *)data_buf, .len = n};
  string_push_slice(&sesh->commands.buffer, cmd);
  session_handle_command_buffer(velvet, sesh);

  if (needs_render) {
    velvet_scene_render(&velvet->scene, velvet_session_render, true, sesh);
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
    if (s->ws.colums && s->ws.lines) {
      string_clear(&scratch);
      string_push_csi(&scratch, 0, INT_SLICE(38, 2, 0x5e, 0x5e, 0x6e), "m");
      // 1. Draw the empty space to the right of this client
      if (s->ws.colums > focused->ws.colums) {
        for (int i = 0; i < focused->ws.lines; i++) {
          string_push_csi(&scratch, 0, INT_SLICE(i + 1, focused->ws.colums + 1), "H");
          int draw_count = s->ws.colums - focused->ws.colums;
          string_push_slice(&scratch, u8_slice_from_cstr(pipe));
          if (--draw_count > 0) string_push_slice(&scratch, u8_slice_from_cstr("·"));
          if (--draw_count > 0) string_push_csi(&scratch, 0, INT_SLICE(draw_count), "b");
        }
      }
      // 2. Draw the empty space below this client
      for (int i = focused->ws.lines; i < s->ws.lines; i++) {
        int draw_count = s->ws.colums;
        string_push_csi(&scratch, 0, INT_SLICE(i + 1, 1), "H");
        if (i == focused->ws.lines) {
          string_push_slice(&scratch, u8_slice_from_cstr(dash));
          draw_count--;
          int n_dashes = MIN(draw_count, focused->ws.colums - 1);
          string_push_csi(&scratch, 0, INT_SLICE(n_dashes), "b");
          draw_count -= n_dashes;
          if (draw_count > 0) string_push_slice(&scratch, u8_slice_from_cstr(corner));
          draw_count--;
        }
        if (draw_count > 0) string_push_slice(&scratch, u8_slice_from_cstr("·"));
        if (--draw_count > 0) string_push_csi(&scratch, 0, INT_SLICE(draw_count), "b");
      }
      string_push_csi(&scratch, 0, INT_SLICE(0), "m");
      string_push_slice(&s->pending_output, string_as_u8_slice(&scratch));
    }
  }
}

static ssize_t session_write_pending(struct velvet_session *sesh) {
  assert(sesh->input);
  assert(sesh->output);
  if (sesh->pending_output.len) {
    ssize_t written = io_write(sesh->output, string_as_u8_slice(&sesh->pending_output));
    velvet_log("Write %zu / %zu\n", written, sesh->pending_output.len);
    if (written > 0) string_drop_left(&sesh->pending_output, (size_t)written);
    return written;
  }
  return -1;
}

static void velvet_render(struct u8_slice str, void *context) {
  struct velvet *a = context;
  struct velvet_session *sesh;
  if (str.len == 0) return;
  vec_foreach(sesh, a->sessions) {
    // output is not set if the session has connected to the socket, but has not yet sent its in/out streams.
    // If the client actually connects, we will send a full render, so there is no need to buffer the write.
    if (sesh->output) {
      string_push_slice(&sesh->pending_output, str);
      session_write_pending(sesh);
    }
  }
}

static void session_input_callback(struct io_source *src, struct u8_slice str) {
  struct velvet *m = src->data;
  if (str.len == 0) {
    struct velvet_session *sesh;
    vec_find(sesh, m->sessions, sesh->input == src->fd);
    if (sesh) velvet_detach_session(m, sesh);
    return;
  }
  velvet_process_input(m, str);

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

static void vte_write_callback(struct io_source *src) {
  struct velvet *v = src->data;
  struct pty_host *vte;
  vec_find(vte, v->scene.hosts, vte->pty == src->fd);
  assert(vte);
  if (vte->emulator.pending_input.len) {
    ssize_t written = io_write(src->fd, string_as_u8_slice(&vte->emulator.pending_input));
    if (written > 0) string_drop_left(&vte->emulator.pending_input, (size_t)written);
  }
}

static void vte_read_callback(struct io_source *src, struct u8_slice str) {
  struct velvet *v = src->data;
  struct pty_host *vte;
  vec_find(vte, v->scene.hosts, vte->pty == src->fd);
  assert(vte);
  pty_host_process_output(vte, str);
}

static void velvet_default_config(struct velvet *v) {
  char *config = "map <C-x>c 'spawn zsh'\n"
                 "map <C-x>d detach\n"
                 "map <C-x>f 'spawn zsh ; detach'\n"
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
  struct platform_winsize ws = {.colums = 80, .lines = 24, .x_pixel = 800, .y_pixel = 600};
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
  velvet_scene_arrange(&velvet->scene);

  velvet_default_config(velvet);

  bool did_resize = false;
  for (;;) {
    velvet_log("Main loop"); // mostly here to detect misbehaving polls.
    struct velvet_session *focus = velvet_get_focused_session(velvet);
    if (focus) {
      if (focus->ws.colums && focus->ws.lines && (focus->ws.colums != velvet->scene.ws.colums || focus->ws.lines != velvet->scene.ws.lines)) {
        velvet_scene_resize(&velvet->scene, focus->ws);
        did_resize = true;
        // Defer redraw until the clients have actually updated. Redrawing right away leads to flickering
        // redraw_needed = true;
      }

      // arrange
      velvet_scene_arrange(&velvet->scene);
      if (did_resize) {
        did_resize = false;
        draw_no_mans_land(velvet);
      }
      // Render the current velvet state
      velvet_scene_render(&velvet->scene, velvet_render, false, velvet);
    }

    // Set up IO
    vec_clear(&loop->sources);

    struct io_source signal_src = { .fd = velvet->signal_read, .events = IO_SOURCE_POLLIN, .read_callback = signal_callback, .data = velvet};
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
        .read_callback = vte_read_callback,
        .write_callback = vte_write_callback,
      };
      if (h->emulator.pending_input.len) read_src.events |= IO_SOURCE_POLLOUT;

      io_add_source(loop, read_src);
    }

    io_add_source(loop, signal_src);

    struct io_source socket_src = { .fd = velvet->socket, .events = IO_SOURCE_POLLIN, .ready_callback = socket_accept, .data = velvet };
    io_add_source(loop, socket_src);

    struct velvet_session *session;
    vec_foreach(session, velvet->sessions) {
      struct io_source socket_src = { .fd = session->socket, .events = IO_SOURCE_POLLIN, .ready_callback = session_socket_callback, .data = velvet };
      io_add_source(loop, socket_src);
      struct io_source input_src = { .fd = session->input, .events = IO_SOURCE_POLLIN, .read_callback = session_input_callback, .data = velvet};
      if (input_src.fd)
        io_add_source(loop, input_src);
      if (session->pending_output.len) {
        struct io_source output_src = { .fd = session->output, .events = IO_SOURCE_POLLOUT, .write_callback = session_output_callback, .data = velvet};
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

