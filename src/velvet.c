#include "velvet.h"
#include "utils.h"
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <csi.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <pwd.h>
#include "velvet_alloc.h"
#include "velvet_lua.h"
#include "platform.h"

void velvet_cmd(struct velvet *v, int source_socket, struct u8_slice cmd);
static void velvet_session_render(struct u8_slice str, void *context) {
  struct velvet_session *s = context;
  string_push_slice(&s->pending_output, str);
}

void velvet_session_destroy(struct velvet *velvet, struct velvet_session *s) {
  if (s->input) close(s->input);
  if (s->output) close(s->output);
  if (s->socket) close(s->socket);
  string_destroy(&s->pending_output);
  string_destroy(&s->command_buffer);
  *s = (struct velvet_session){0};
  size_t idx = vec_index(&velvet->sessions, s);
  vec_remove_at(&velvet->sessions, idx);
}

/* Occasionally under MacOS, the screen is not fully redrawn after waking from sleep.
 * I have personally observed this under Ghostty, but it's not clear if it's an issue in other emulators.
 * Forcing a full redraw on focus regain works around it. */
void velvet_force_full_redraw(struct velvet *v) {
  v->scene.force_redraw = true;
  velvet_invalidate_render(v, "full redraw requested");
}

void velvet_detach_session(struct velvet *velvet, struct velvet_session *s) {
  if (!s) return;
  int sock = s->socket;
  if (s->socket) {
    uint8_t detach = 'D';
    write(s->socket, &detach, 1);
    close(s->socket);
  }
  velvet_session_destroy(velvet, s);
  if (sock && velvet->focused_socket == sock) {
    struct velvet_session *fst = NULL;
    vec_find(fst, velvet->sessions, fst->socket && fst->input);
    velvet->focused_socket = fst ? fst->socket : 0;
  }
}

void velvet_set_focused_session(struct velvet *v, int socket_fd) {
  struct velvet_session *s;
  vec_find(s, v->sessions, s->socket == socket_fd);
  if (s) v->focused_socket = socket_fd;
}

struct velvet_session *velvet_get_focused_session(struct velvet *v) {
  if (v->sessions.length && v->focused_socket) {
    struct velvet_session *f;
    vec_find(f, v->sessions, f->socket == v->focused_socket);
    return f;
  }
  return NULL;
}

static void session_handle_command_buffer(struct velvet *v, struct velvet_session *src) {
  int socket = src->socket;
  struct u8_slice cmd = string_as_u8_slice(src->command_buffer);
  velvet_cmd(v, socket, cmd);
  string_clear(&src->command_buffer);
}

static void session_handle_lua_chunk(struct velvet *v, struct vv_lua_payload *chunk, int mapfd, int session_fd) {
  struct velvet_alloc *a = velvet_alloc_shmem_remap(mapfd);
  struct u8_slice code = { .content = (uint8_t*)a + chunk->chunk_offset, .len = chunk->chunk_length };
  size_t *string_offsets = (size_t*)((uint8_t*)a + chunk->args_offset);

  /* intentional VLA */
  char *varargs[chunk->args_count];
  for (size_t i = 0; i < chunk->args_count; i++) {
    char *arg = (char *)a + string_offsets[i];
    varargs[i] = arg;
  }

  struct velvet_lua_context ctx = {
    .n = chunk->args_count,
    .args = varargs,
    .cwd = chunk->cwd_offset ? (char *)a + chunk->cwd_offset : v->startup_directory,
  };
  velvet_lua_execute_chunk(v, code, session_fd, ctx);
  velvet_alloc_shmem_destroy(a, mapfd);
}

static void session_socket_callback(struct io_source *src) {
  struct velvet *velvet = src->data;
  char data_buf[8192] = {0};
  char cmsgbuf[CMSG_SPACE(sizeof(int) * 3)] = {0};
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
  /* this happens because the session was converted to a coroutine */
  if (!session) return;

  if (n >= (int)sizeof(data_buf)) {
    const char *err = "error: lua chunk too large (max 8kb); use dofile() for larger scripts\n";
    write(src->fd, err, strlen(err));
    velvet_session_destroy(velvet, session);
    return;
  }

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
    int fds[3];
    memcpy(fds, CMSG_DATA(cmsg), sizeof(fds));

    /* if this is a lua chunk, the fd will be a shared memory map.
     * Otherwise the fds will be in/out handles */
    if (n == sizeof(struct vv_lua_payload)) {
      struct vv_lua_payload *lua_chunk = (struct vv_lua_payload *)data_buf;
      if (lua_chunk->magic == VV_LUA_MAGIC) {
        /* convert this velvet_session to a coroutine */
        session->socket = 0;
        velvet_session_destroy(velvet, session);
        struct velvet_coroutine *co = vec_new_element(&velvet->coroutines);
        co->socket = src->fd;
        int map_fd = fds[0];
        co->out_fd = fds[1];
        co->err_fd = fds[2];
        set_cloexec(map_fd);
        set_cloexec(co->out_fd);
        set_cloexec(co->err_fd);
        set_nonblocking(co->out_fd);
        set_nonblocking(co->err_fd);
        session_handle_lua_chunk(velvet, lua_chunk, map_fd, src->fd);
        vec_find(session, velvet->sessions, session->socket == src->fd);
        if (session && (!session->input || !session->output)) {
          velvet_session_destroy(velvet, session);
        }
        return;
      }
    }
    session->input = fds[0];
    session->output = fds[1];
    set_cloexec(session->input);
    set_cloexec(session->output);

    set_nonblocking(session->output);

    // Since we are normally only rendering lines which have changed,
    // new clients must receive a complete render upon connecting.
    velvet->focused_socket = session->socket;
    needs_render = true;
  }

  struct u8_slice cmd = {.content = (uint8_t *)data_buf, .len = n};
  string_push_slice(&session->command_buffer, cmd);
  session_handle_command_buffer(velvet, session);

  /* ensure the session still exists after handling the command */
  vec_find(session, velvet->sessions, session->socket == src->fd);
  if (!session) return;

  if (!session->input || !session->output) {
    velvet_session_destroy(velvet, session);
  } else if (needs_render) {
    velvet_scene_render_full(&velvet->scene, velvet_session_render, session);
  }
}

static void socket_accept(struct io_source *src) {
  struct velvet *velvet = src->data;

  int client_fd = accept(src->fd, NULL, NULL);
  if (client_fd == -1) {
    ERROR("accept:");
    return;
  }
  set_cloexec(client_fd);

  struct velvet_session c = { .socket = client_fd };
  vec_push(&velvet->sessions, &c);
}

void velvet_scene_remove_exited(struct velvet *v) {
  int status;
  pid_t pid = 0;
  struct velvet_scene *m = &v->scene;

  while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
    struct velvet_window *h;
    vec_find(h, m->windows, h->pid == pid);
    if (h) {
      h->pid = 0;
      h->exited_at = get_ms_since_startup();
      velvet_scene_close_and_remove_window(&v->scene, h);
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

static void on_coroutine_hangup(struct io_source *src) {
  struct velvet *velvet = src->data;
  struct velvet_coroutine *co;
  vec_find(co, velvet->coroutines, co->socket == src->fd);
  if (co) {
    velvet_coroutine_destroy(velvet, co);
  }
}

static void on_coroutine_socket_read(struct io_source *src, struct u8_slice str) {
  if (str.len == 0) on_coroutine_hangup(src);
}

static void on_session_input(struct io_source *src, struct u8_slice str) {
  struct velvet *v = src->data;
  struct velvet_session *session;
  vec_find(session, v->sessions, session->input == src->fd);

  if (str.len == 0) {
    velvet_detach_session(v, session);
    return;
  }

  if (session) v->input.input_socket = session->socket;
  velvet_input_process(v, str);
  v->input.input_socket = 0;
}

static bool coroutine_maybe_destroy(struct velvet *v, struct velvet_coroutine *co) {
  if (co->coroutine == NULL && co->pending_output.len == 0 && co->pending_error.len == 0) {
    velvet_coroutine_destroy(v, co);
    return true;
  }
  return false;
}

static void co_write(struct velvet *v, struct velvet_coroutine *co, int fd, struct string *buf) {
  if (buf->len) {
    ssize_t written = io_write(fd, string_as_u8_slice(*buf));
    if (written > 0) {
      string_shift_left(buf, written);
    } else if (written == -1 && errno == EPIPE ) {
      velvet_coroutine_destroy(v, co);
      return;
    } else if (written == 0) {
      velvet_coroutine_destroy(v, co);
      return;
    }
  }
  coroutine_maybe_destroy(v, co);
}

static void on_coroutine_error_writable(struct io_source *src) {
  struct velvet *velvet = src->data;
  struct velvet_coroutine *co;
  vec_find(co, velvet->coroutines, co->err_fd == src->fd);
  if (co) co_write(velvet, co, co->err_fd, &co->pending_error);
}

static void on_coroutine_writable(struct io_source *src) {
  struct velvet *velvet = src->data;
  struct velvet_coroutine *co;
  vec_find(co, velvet->coroutines, co->out_fd == src->fd);
  if (co) co_write(velvet, co, co->out_fd, &co->pending_output);
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
  struct velvet_window *win;
  vec_find(win, v->scene.windows, win->write == src->fd);
  assert(win);
  if (win->emulator.pending_input.len) {
    ssize_t written = io_write(src->fd, string_as_u8_slice(win->emulator.pending_input));
    if (written > 0) string_shift_left(&win->emulator.pending_input, (size_t)written);
    else if (written == 0) velvet_scene_remove_exited(v);
  }
  if (win->emulator.pending_input.len == 0) 
    src->events &= ~IO_SOURCE_POLLOUT;
}

static bool rects_intersect(struct rect a, struct rect b) {
  return a.left < b.left + b.width && a.left + a.width > b.left && a.top < b.top + b.height && a.top + a.height > b.top;
}

bool window_visible(struct velvet *v, struct velvet_window *w) {
  if (w->hidden) return false;
  return rects_intersect(v->scene.size, w->geometry);
}

static void on_window_output(struct io_source *src, struct u8_slice str) {
  struct velvet *v = src->data;
  if (str.len) {
    struct velvet_window *vte;
    vec_find(vte, v->scene.windows, vte->read == src->fd);
    assert(vte);
    velvet_window_process_output(vte, str);

    if (vte->emulator_output_buffer.len) {
      struct velvet_session *session;
      /* multicast output to all sessions. In practice, there will only be one session connected,
       * but since there is no good way to determine if a session is a proper terminal emulator just send it to every
       * session with an output pipe. The worst case is something like the system clipboard being set multiple times
       * which is harmless. */
      vec_where(session, v->sessions, session->output) {
        string_push_string(&session->pending_output, vte->emulator_output_buffer);
      }

      /* Consider the output handled even if it was not transmitted to any client.
       * We really don't want the buffer to accumulate when no sessions are connected,
       * and allowing a process to e.g. set the clipboard when no client is connected
       * is kind of an anti-feature anyway,
       */
      string_clear(&vte->emulator_output_buffer);
    }

    vte->had_output = true;

    if (window_visible(v, vte)) {
      velvet_invalidate_render(v, "window output");
    }
  } else {
    velvet_scene_remove_exited(v);
    /* the window should have been removed in the remove_exited() call,
     * but in case it was not detected we explicitly remove it by pty here */
    struct velvet_window *vte;
    vec_find(vte, v->scene.windows, vte->read == src->fd);
    if (vte) {
      velvet_scene_close_and_remove_window(&v->scene, vte);
    }
  }
}

static bool velvet_align_and_arrange(struct velvet *v, struct velvet_session *focus) {
  bool resized = false;
  if (focus->ws.width && focus->ws.height && (focus->ws.width != v->scene.size.width || focus->ws.height != v->scene.size.height)) {
    velvet_scene_resize(&v->scene, focus->ws);
    resized = true;
    velvet_invalidate_render(v, "terminal resized");
  }
  return resized;
}

static void velvet_raise_window_output_events(struct velvet *v) {
  struct velvet_window *win;
  vec_where(win, v->scene.windows, win->had_output) {
    win->had_output = false;
    struct velvet_api_window_output_event_args event_args = {.win_id = win->id};
    velvet_api_raise_window_output(v, event_args);
  }
}

static void velvet_dispatch_frame(void *data) {
  struct velvet *v = data;

  struct velvet_session *focus = velvet_get_focused_session(v);
  if (focus) {
    bool is_idle = io_schedule_exists(&v->event_loop, v->active_render_token);
    v->scene.renderer.options.no_repeat_multibyte_symbols = focus->features.no_repeat_multibyte_graphemes;
    struct velvet_api_pre_render_event_args event_args = {
        .time = get_ms_since_startup(),
        .cause = v->render_invalidate_reason ? u8_slice_from_cstr(v->render_invalidate_reason)
                 : is_idle                   ? u8_slice_from_cstr("io_idle")
                                             : u8_slice_from_cstr("io_busy")
    };
    velvet_api_raise_pre_render(v, event_args);
    velvet_raise_window_output_events(v);
    velvet_scene_render_damage(&v->scene, velvet_render, v);
  }

  v->_render_invalidated = false;
  v->render_invalidate_reason = NULL;
  io_schedule_cancel(&v->event_loop, v->active_render_token);
  io_schedule_cancel(&v->event_loop, v->idle_render_token);
}

void velvet_invalidate_render(struct velvet *velvet, [[maybe_unused]] const char *reason) {
  velvet->_render_invalidated = true;
  velvet->render_invalidate_reason = reason;
}

static void velvet_ensure_render_scheduled(struct velvet *velvet) {
  if (!io_schedule_exists(&velvet->event_loop, velvet->idle_render_token)) {
    /* schedule a render as soon as io is idle */
    velvet->idle_render_token = io_schedule_idle(&velvet->event_loop, velvet_dispatch_frame, velvet);
  }
  if (!io_schedule_exists(&velvet->event_loop, velvet->active_render_token)) {
    /* or schedule a render within a reasonable time */
    velvet->active_render_token =
        io_schedule(&velvet->event_loop, velvet->min_ms_per_frame, velvet_dispatch_frame, velvet);
  }
  velvet->_render_invalidated = false;
}

static void velvet_dispatch(struct velvet *velvet) {
  struct io *const loop = &velvet->event_loop;
  struct velvet_session *focus = velvet_get_focused_session(velvet);
  if (focus) velvet_align_and_arrange(velvet, focus);
  if (velvet->_render_invalidated) velvet_ensure_render_scheduled(velvet);

  // Set up IO
  vec_clear(&loop->sources);

  struct io_source signal_src = {
      .fd = velvet->signal_read, .events = IO_SOURCE_POLLIN, .on_read = on_signal, .data = velvet};
  struct velvet_window *h;
  vec_where(h, velvet->scene.windows, h->pty && h->pid) {
    struct io_source read_src = {
        .data = velvet,
        .fd = h->read,
        .events = IO_SOURCE_POLLIN,
        .on_read = on_window_output,
    };
    io_add_source(loop, read_src);
    if (h->emulator.pending_input.len) {
      struct io_source write_src = {
          .data = velvet,
          .fd = h->write,
          .events = IO_SOURCE_POLLOUT,
          .on_writable = on_window_writable,
      };
      io_add_source(loop, write_src);
    }
  }

  io_add_source(loop, signal_src);

  struct io_source socket_src = {
      .fd = velvet->socket, .events = IO_SOURCE_POLLIN, .on_readable = socket_accept, .data = velvet};
  io_add_source(loop, socket_src);

  struct velvet_session *session;
  vec_foreach(session, velvet->sessions) {
    struct io_source socket_src = {
        .fd = session->socket, .events = IO_SOURCE_POLLIN, .on_readable = session_socket_callback, .data = velvet};
    io_add_source(loop, socket_src);
    struct io_source input_src = {
        .fd = session->input, .events = IO_SOURCE_POLLIN, .on_read = on_session_input, .data = velvet};
    if (input_src.fd) io_add_source(loop, input_src);
    if (session->pending_output.len) {
      struct io_source output_src = {
          .fd = session->output, .events = IO_SOURCE_POLLOUT, .on_writable = on_session_writable, .data = velvet};
      if (output_src.fd) io_add_source(loop, output_src);
    }
  }

  struct velvet_coroutine *co;
  vec_rforeach(co, velvet->coroutines) {
    if (co->coroutine == NULL) {
      /* handle finished coroutines which never had any output */
      if (coroutine_maybe_destroy(velvet, co)) continue;
    }
    struct io_source socket_src = {
        .data = velvet,
        .fd = co->socket,
        /* monitor the socket closing. This disposes the coroutine */
        .on_hangup = on_coroutine_hangup,
        /* on MacOS, POLLHUP isn't raised when .events=0. To fix this, we always listen for POLLIN.
         * This is fine since coroutine sockets don't communicate after the initial connection. */
        .on_read = on_coroutine_socket_read,
        .events = IO_SOURCE_POLLIN,
    };
    /* unconditionally monitor socket for hangup */
    io_add_source(loop, socket_src);

    if (co->pending_output.len > 0) {
      struct io_source out_src = {
          .data = velvet,
          .fd = co->out_fd,
          .events = IO_SOURCE_POLLOUT,
          .on_writable = on_coroutine_writable,
      };
      io_add_source(loop, out_src);
    }

    if (co->pending_error.len > 0) {
      struct io_source err_src = {
          .data = velvet,
          .fd = co->err_fd,
          .events = IO_SOURCE_POLLOUT,
          .on_writable = on_coroutine_error_writable,
      };
      io_add_source(loop, err_src);
    }
  }

  // Dispatch all pending io
  io_dispatch(loop);
  velvet_raise_window_output_events(velvet);
}

void velvet_loop(struct velvet *velvet) {
  // Set an initial dummy size. This will be controlled by clients once they connect.
  struct rect ws = {.width = 80, .height = 24, .x_pixel = 800, .y_pixel = 600};
  velvet_scene_resize(&velvet->scene, ws);
  /* We need to pass in a velvet reference to the scene so it can raise events.
   * TODO: Get rid of `velvet_scene` and store windows directly in `velvet`.
   * Scene initially made sense because it managed and arranged windows.
   * Now it's just a dumb container. */
  velvet->scene.v = velvet;

  velvet_lua_init(velvet);
  velvet_source_config(velvet);

  /* arbitrarily set a minimum update rate of 40 fps
   * Setting a higher update rate reduces throughput in extreme scenarios.
   * For most updates, this timeout will not be hit at all since IO will normally be idle at some point.
   * */
  velvet->min_ms_per_frame = 1000 * (1.0 / 40);
  velvet_invalidate_render(velvet, "initial render");

  for (; !velvet->quit;) {
    velvet_log("Main loop"); // mostly here to detect misbehaving polls.
    velvet_dispatch(velvet);
  }
}

void velvet_destroy(struct velvet *velvet) {
  io_destroy(&velvet->event_loop);
  velvet_scene_destroy(&velvet->scene);
  velvet_input_destroy(&velvet->input);
  while (velvet->sessions.length) {
    velvet_session_destroy(velvet, vec_nth(velvet->sessions, 0));
  }
  while (velvet->coroutines.length) {
    velvet_coroutine_destroy(velvet, vec_nth(velvet->coroutines, 0));
  }
  vec_destroy(&velvet->sessions);
  struct velvet_kvp *kvp;
  vec_foreach(kvp, velvet->stored_strings) {
    string_destroy(&kvp->key);
    string_destroy(&kvp->value);
  }
  vec_destroy(&velvet->stored_strings);
  if (velvet->L) lua_close(velvet->L);
}

