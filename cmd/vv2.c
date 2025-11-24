#include <signal.h>
#include <utils.h>
#include <vte_host.h>
#include <collections.h>
#include <poll.h>
#include <errno.h>
#include <io.h>
#include <multiplexer.h>
#include "platform.h"
#include "virtual_terminal_sequences.h"

static int signal_write;
static int signal_read;
static void signal_handler(int sig, siginfo_t *siginfo, void *context) {
  (void)siginfo, (void)context;
  size_t written = write(signal_write, &sig, sizeof(sig));
  if (written < sizeof(sig)) die("signal write:");
}

static void install_signal_handlers(void) {
  struct sigaction sa = {0};
  sa.sa_sigaction = &signal_handler;
  sa.sa_flags = SA_SIGINFO;
  if (sigaction(SIGWINCH, &sa, NULL) == -1) {
    die("sigaction:");
  }
  if (sigaction(SIGINT, &sa, NULL) == -1) {
    die("sigaction:");
  }
  if (sigaction(SIGCHLD, &sa, NULL) == -1) {
    die("sigaction:");
  }

  int pipes[2];
  if (pipe(pipes) < 0) {
    die("pipe:");
  }
  signal_read = pipes[0];
  signal_write = pipes[1];
}



struct app_context {
  struct multiplexer multiplexer;
  struct string draw_buffer;
  bool quit;
};

static void signal_callback(struct io_source *src, uint8_t *buffer, int n) {
  struct app_context *app = src->data;
  // 1. Dispatch any pending signals
  bool did_resize = false;
  bool did_sigchld = false;
  int *signals = (int*)buffer;
  for (int i = 0; i < (int)(n / sizeof(int)); i++) {
    int signal = signals[i];
    switch (signal) {
    case SIGWINCH: {
      did_resize = true;
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

  if (did_sigchld) multiplexer_remove_exited(&app->multiplexer);
  if (did_resize) {
    int rows, columns;
    platform_get_winsize(&rows, &columns);
    multiplexer_resize(&app->multiplexer, rows, columns);
  }
}

static void stdin_callback(struct io_source *src, uint8_t *buffer, int n) {
  struct app_context *m = src->data;
  if (n == 0) {
    m->quit = true;
    return;
  }
  multiplexer_feed_input(&m->multiplexer, buffer, n);
}

static void read_callback(struct io_source *src, uint8_t *buffer, int n) {
  struct app_context *m = src->data;
  struct vte_host *vte;
  vec_foreach(vte, m->multiplexer.clients) {
    if (vte->pty == src->fd) {
      vte_host_process_output(vte, buffer, n);
      break;
    }
  }
}

static void render_frame(struct app_context *app, int target) {
  if (app->multiplexer.clients.length == 0) return;
  static enum cursor_style current_cursor_style = 0;
  struct string *draw_buffer = &app->draw_buffer;
  struct vte_host *focused = vec_nth(app->multiplexer.clients, app->multiplexer.focus);

  string_push_slice(draw_buffer, vt_hide_cursor);
  for (size_t i = 0; i < app->multiplexer.clients.length; i++) {
    struct vte_host *h = vec_nth(app->multiplexer.clients, i);
    vte_host_update_cwd(h);
    vte_host_draw(h, false, draw_buffer);
    vte_host_draw_border(h, draw_buffer, i == app->multiplexer.focus);
  }

  {
    // move cursor to focused host
    struct grid *g = focused->vte.active_grid;
    struct cursor *c = &g->cursor;
    int lineno = 1 + focused->rect.client.y + c->row;
    int columnno = 1 + focused->rect.client.x + c->col;
    string_push_csi(draw_buffer, INT_SLICE(lineno, columnno), "H");
  }

  // set the cursor style according to the focused client.
  if (focused->vte.options.cursor.style != current_cursor_style) {
    current_cursor_style = focused->vte.options.cursor.style;
    string_push_csi(draw_buffer, INT_SLICE(current_cursor_style), " q");
  }

  // Set cursor visibility according to the focused client.
  if (focused->vte.options.cursor.visible) string_push_slice(draw_buffer, vt_show_cursor);

  string_flush(&app->draw_buffer, target, nullptr);
}

int main(int argc, char **argv) {
  terminal_setup();
  install_signal_handlers();

  struct app_context app = {.multiplexer = multiplexer_default};
  int rows, columns;
  platform_get_winsize(&rows, &columns);
  multiplexer_resize(&app.multiplexer, rows, columns);

  if (argc < 2) {
    multiplexer_spawn_process(&app.multiplexer, "zsh");
  }

  for (int i = 1; i < argc; i++) {
    multiplexer_spawn_process(&app.multiplexer, argv[i]);
  }

  struct io io = io_default;

  for (;;) {
    // Set up IO
    vec_clear(&io.sources);
    struct io_source stdin_src = {.fd = STDIN_FILENO, .events = POLL_IN, .on_data = stdin_callback, .data = &app};
    struct io_source signal_src = {.fd = signal_read, .events = POLL_IN, .on_data = signal_callback, .data = &app};
    io_add_source(&io, signal_src);
    io_add_source(&io, stdin_src);
    struct vte_host *h;
    vec_foreach(h, app.multiplexer.clients) {
      struct io_source src = {.fd = h->pty, .events = POLL_IN, .on_data = read_callback, .data = &app};
      io_add_source(&io, src);
    }

    // Dispatch all pending io
    io_flush(&io);

    // quit ?
    if (app.multiplexer.clients.length == 0 || app.quit) break;

    // arrange
    multiplexer_arrange(&app.multiplexer);

    // Render the current app state
    render_frame(&app, STDOUT_FILENO);
  }

  terminal_reset();
  printf("[exited]\n");

  return 0;
}
