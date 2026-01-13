#include "velvet.h"
#include "utils.h"
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <csi.h>
#include <string.h>
#include "velvet_cmd.h"
#include <signal.h>
#include <sys/wait.h>
#include <pwd.h>
#include "velvet_lua.h"

static void velvet_session_render(struct u8_slice str, void *context) {
  struct velvet_session *s = context;
  string_push_slice(&s->pending_output, str);
}

void velvet_session_destroy(struct velvet *velvet, struct velvet_session *s) {
  if (s->input) close(s->input);
  if (s->output) close(s->output);
  if (s->socket) close(s->socket);
  string_destroy(&s->pending_output);
  string_destroy(&s->commands.buffer);
  string_destroy(&s->cwd);
  *s = (struct velvet_session){0};
  size_t idx = vec_index(&velvet->sessions, s);
  vec_remove_at(&velvet->sessions, idx);
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
    struct velvet_session *fst = nullptr;
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
    return;
  }

  struct velvet_session c = { .socket = client_fd };
  vec_push(&velvet->sessions, &c);
}

static void velvet_scene_remove_schedule(void *data) {
  struct velvet *v = data;
  uint64_t now = get_ms_since_startup();
  struct velvet_window *h;
  vec_rwhere(h,
             v->scene.windows,
             (h->close.when & VELVET_WINDOW_CLOSE_AFTER_DELAY) && h->exited_at &&
                 h->exited_at + h->close.delay_ms <= now) {
    velvet_scene_close_and_remove_window(&v->scene, h);
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
      if ((h->close.when & VELVET_WINDOW_CLOSE_AFTER_DELAY) && h->close.delay_ms > 0) {
        if (h->pty > 0) {
          /* close the pty, but defer cleanup */
          close(h->pty);
          h->pty = 0;
        }
        io_schedule(&v->event_loop, h->close.delay_ms + 1, velvet_scene_remove_schedule, v);
      } else {
        velvet_scene_close_and_remove_window(&v->scene, h);
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

  velvet_ensure_render_scheduled(v);
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
  vec_find(win, v->scene.windows, win->pty == src->fd);
  assert(win);
  if (win->emulator.pending_input.len) {
    ssize_t written = io_write(src->fd, string_as_u8_slice(win->emulator.pending_input));
    if (written > 0) string_shift_left(&win->emulator.pending_input, (size_t)written);
    else if (written == 0) velvet_scene_remove_exited(v);
  }
  if (win->emulator.pending_input.len == 0) 
    src->events &= ~IO_SOURCE_POLLOUT;
}

static void on_window_output(struct io_source *src, struct u8_slice str) {
  struct velvet *v = src->data;
  if (str.len) {
    struct velvet_window *vte;
    vec_find(vte, v->scene.windows, vte->pty == src->fd);
    assert(vte);
    velvet_window_process_output(vte, str);
  } else {
    velvet_scene_remove_exited(v);
  }
  velvet_ensure_render_scheduled(v);
}

static void velvet_default_config(struct velvet *v) {
  struct string config = {0};
  string_push_cstr(&config,
                   "map -r         <C-x>x       incborder\n"
                   "map -r         <C-x><C-c>   cycle-transparency\n"
                   "map -r         <C-x>a       decborder\n"
                   "map -r         <C-x>[       decfactor\n"
                   "map -r         <C-x>]       incfactor\n"
                   "map -r         <C-x>i       incnmaster\n"
                   "map -r         <C-x>o       decnmaster\n"
                   );

  struct u8_slice cfg = string_as_u8_slice(config);
  struct velvet_cmd_iterator it = {.src = cfg};
  while (velvet_cmd_iterator_next(&it)) {
    velvet_cmd(v, 0, it.current);
  }
  string_destroy(&config);
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

static bool velvet_align_and_arrange(struct velvet *v, struct velvet_session *focus) {
  bool resized = false;
  if (focus->ws.w && focus->ws.h && (focus->ws.w != v->scene.ws.w || focus->ws.h != v->scene.ws.h)) {
    velvet_scene_resize(&v->scene, focus->ws);
    resized = true;
  }
  v->scene.arrange(&v->scene);
  return resized;
}

static void string_push_color(struct string *str, struct color rgb, bool fg) {
  assert(rgb.cmd == COLOR_RGB);
  int cmd = fg ? 38 : 48;
  string_push_format_slow(str, "\x1b[%d;2;%d;%d;%dm", cmd, rgb.r, rgb.g, rgb.b);
}
static void string_push_bg(struct string *str, struct color rgb) {
  string_push_color(str, rgb, false);
}
static void string_push_fg(struct string *str, struct color rgb) {
  string_push_color(str, rgb, true);
}

static uint64_t status_winid = 1;

static bool occupied(struct velvet_scene *s, uint32_t tag) {
  struct velvet_window *w;
  vec_where(w,
            s->windows,
            (w->layer == VELVET_LAYER_FLOATING || w->layer == VELVET_LAYER_TILED) && w->tags & tag) return true;
  return false;
}

static void draw_status(struct velvet *v) {
  static struct string buf = {0};
  string_clear(&buf);
  struct velvet_window *status = velvet_scene_get_window_from_id(&v->scene, status_winid);
  assert(status);
  assert(!status->emulator.pending_input.len);
  int numtags = 9;

  struct velvet_theme t = v->scene.theme;
  string_push_bg(&buf, t.mantle);
  string_push_fg(&buf, t.mantle);
  string_push_format_slow(&buf, "\r\x1b[K");

  for (int i = 0; i < numtags; i++) {
    int tagmask = 1 << i;
    bool tag_visible = v->scene.view & tagmask;
    if (tag_visible) {
      string_push_bg(&buf, t.status.visible);
    } else if (occupied(&v->scene, tagmask)) {
      string_push_bg(&buf, t.status.not_visible);
    } else {
      /* hide empty tags */
      continue;
    }

    string_push_format_slow(&buf, " %d ", i + 1);
  }

  string_push_bg(&buf, t.mantle);
  string_push_fg(&buf, t.foreground);

  struct velvet_key_event pending[10] = {0};
  int n_pending = 0;
  struct velvet_keymap *k = v->input.keymap;
  while (k && n_pending < LENGTH(pending)) {
    if (k == k->root) break;
    pending[n_pending++] = k->key;
    k = k->parent;
  }

  if (n_pending) {
    string_push_cstr(&buf, "  ");
    string_push_bg(&buf, t.background);
    string_push_cstr(&buf, " ");
    for (int i = 0; i < n_pending; i++) {
      struct velvet_key_event e = pending[n_pending - i - 1];
      if (e.modifiers || e.key.name) string_push_cstr(&buf, "<");
      if (e.modifiers & MODIFIER_SHIFT) string_push_cstr(&buf, "S-");
      if (e.modifiers & MODIFIER_ALT) string_push_cstr(&buf, "M-");
      if (e.modifiers & MODIFIER_CTRL) string_push_cstr(&buf, "C-");
      if (e.modifiers & MODIFIER_SUPER) string_push_cstr(&buf, "D-");

      if (e.key.name) {
        string_push_cstr(&buf, e.key.name);
      } else if (e.key.codepoint) {
        struct utf8 u = {0};
        int n = codepoint_to_utf8(e.key.codepoint, &u);
        string_push_range(&buf, u.utf8, n);
      }

      if (e.modifiers || e.key.name) string_push_cstr(&buf, ">");
    }
    string_push_cstr(&buf, " ");
  }
  string_push_bg(&buf, t.mantle);


  vte_process(&status->emulator, string_as_u8_slice(buf));
}

static void velvet_dispatch_frame(void *data) {
  struct velvet *v = data;

  struct velvet_session *focus = velvet_get_focused_session(v);
  if (focus) {
    velvet_align_and_arrange(v, focus);
    draw_status(v);
    v->scene.renderer.options.no_repeat_multibyte_symbols = focus->features.no_repeat_wide_chars;
    velvet_scene_render_damage(&v->scene, velvet_render, v);
  }
  io_schedule_cancel(&v->event_loop, v->active_render_token);
  io_schedule_cancel(&v->event_loop, v->idle_render_token);
}

void velvet_ensure_render_scheduled(struct velvet *velvet) {
  if (!io_schedule_exists(&velvet->event_loop, velvet->idle_render_token)) {
    /* schedule a render as soon as io is idle */
    velvet->idle_render_token = io_schedule_idle(&velvet->event_loop, velvet_dispatch_frame, velvet);
  }
  if (!io_schedule_exists(&velvet->event_loop, velvet->active_render_token)) {
    /* or schedule a render within a reasonable time */
    velvet->active_render_token =
        io_schedule(&velvet->event_loop, velvet->min_ms_per_frame, velvet_dispatch_frame, velvet);
  }
}

static bool file_exists(const char *path) {
  struct stat st;
  return stat(path, &st) == 0;
}

static void velvet_source_config(struct velvet *v) {
  char path[PATH_MAX];
  char *home = getenv("HOME");
  if (home) {
    snprintf(path, PATH_MAX - 1, "%s/.config/velvet/init.lua", home);
    if (file_exists(path)) {
      velvet_lua_source(v, path);
    }
  }
}

static void velvet_emit_event(struct velvet *v, const char *evt, int data) {
  lua_State *L = v->L;
  // vv.emit_event(evt, data)
  lua_getglobal(L, "vv");
  lua_getfield(L, -1, "events");
  lua_getfield(L, -1, "emit_event");
  lua_pushstring(L, evt);
  lua_pushinteger(L, data);
  lua_pcall(L, 2, 0, 0); 
}

static void on_screen_resized(void *data) {
  velvet_emit_event(data, "screen_resized", 0);
}
static void on_window_created(int win_id, void *data) {
  velvet_emit_event(data, "window_created", win_id);
}
static void on_window_removed(int win_id, void *data) {
  velvet_emit_event(data, "window_removed", win_id);
}
static void on_window_moved(int win_id, void *data) {
  velvet_emit_event(data, "window_moved", win_id);
}
static void on_window_resized(int win_id, void *data) {
  velvet_emit_event(data, "window_resized", win_id);
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

  velvet_default_config(velvet);
  velvet_lua_init(velvet);

  {
    struct velvet_scene *s = &velvet->scene;
    s->events.data = velvet;
    s->events.on_screen_resized = on_screen_resized;
    s->events.on_window_created = on_window_created;
    s->events.on_window_removed = on_window_removed;
    s->events.on_window_moved = on_window_moved;
    s->events.on_window_resized = on_window_resized;
  }

  velvet_source_config(velvet);

  velvet_scene_resize(&velvet->scene, ws);
  start_default_shell(velvet);

  {
    /* create status window */
    struct velvet_window status = {.id = status_winid, .layer = VELVET_LAYER_STATUS};
    status.emulator.options.alternate_screen = true;
    velvet_scene_manage(&velvet->scene, status);
  }

  velvet->scene.arrange(&velvet->scene);

  velvet->min_ms_per_frame = 10;
  velvet_ensure_render_scheduled(velvet);

  for (;;) {
    velvet_log("Main loop"); // mostly here to detect misbehaving polls.
    struct velvet_session *focus = velvet_get_focused_session(velvet);
    if (focus) velvet_align_and_arrange(velvet, focus);

    // Set up IO
    vec_clear(&loop->sources);

    struct io_source signal_src = { .fd = velvet->signal_read, .events = IO_SOURCE_POLLIN, .on_read = on_signal, .data = velvet};
    struct velvet_window *h;
    vec_where(h, velvet->scene.windows, h->pty && h->pid) {
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
             real_client->kind == VELVET_WINDOW_PTY_HOST &&
                 (real_client->layer == VELVET_LAYER_TILED || real_client->layer == VELVET_LAYER_FLOATING));

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
  while (velvet->sessions.length) {
    velvet_session_destroy(velvet, vec_nth(velvet->sessions, 0));
  }
  vec_destroy(&velvet->sessions);
  lua_close(velvet->L);
}

