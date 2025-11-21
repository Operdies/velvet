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

#ifndef CTRL
#define CTRL(x) ((x) & 037)
#endif

static struct {
  union {
    int pipes[2];
    struct {
      int read;
      int write;
    };
  };
} sigpipe;

static void signal_handler(int sig, siginfo_t *siginfo, void *context) {
  (void)siginfo, (void)context;
  size_t written = write(sigpipe.write, &sig, sizeof(sig));
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

  if (pipe(sigpipe.pipes) < 0) {
    die("pipe:");
  }
  set_nonblocking(sigpipe.read);
}



struct app_context {
  struct multiplexer multiplexer;
  struct string draw_buffer;
  bool quit;
};

static void read_callback(uint8_t *buffer, int n, struct io_source *src, void *data) {
  struct app_context *m = data;
  if (src->fd == STDIN_FILENO) {
    if (n == 0) {
      m->quit = true;
      return;
    }
    multiplexer_feed_input(&m->multiplexer, buffer, n);
  } else {
    struct vte_host *vte;
    vec_foreach(vte, m->multiplexer.hosted) {
      if (vte->pty == src->fd) {
        vte_host_process_output(vte, buffer, n);
        break;
      }
    }
  }
}

static void render_frame(struct app_context *app) {
  static enum cursor_style current_cursor_style = 0;
  struct string *draw_buffer = &app->draw_buffer;
  struct vte_host *focused = vec_nth(app->multiplexer.hosted, app->multiplexer.focus);

  string_push_slice(draw_buffer, vt_hide_cursor);
  for (size_t i = 0; i < app->multiplexer.hosted.length; i++) {
    struct vte_host *h = vec_nth(app->multiplexer.hosted, i);
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
}

int main(int argc, char **argv) {
  terminal_setup();
  install_signal_handlers();

  set_nonblocking(STDIN_FILENO);

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

  struct io io = {.sources = vec(struct io_source), .pollfds = vec(struct pollfd)};

  while (app.multiplexer.hosted.length && !app.quit) {
    // 1. Dispatch any pending signals
    int signal = 0;
    bool did_resize = false;
    bool did_sigchld = false;
    while (read(sigpipe.read, &signal, sizeof(signal)) > 0) {
      switch (signal) {
      case SIGWINCH: {
        did_resize = true;
      } break;
      case SIGCHLD: {
        did_sigchld = true;
      } break;
      default:
        app.quit = true;
        logmsg("Unhandled signal: %d", signal);
        break;
      }
    }

    if (did_sigchld) multiplexer_remove_exited(&app.multiplexer);
    if (did_resize) {
      platform_get_winsize(&rows, &columns);
      multiplexer_resize(&app.multiplexer, rows, columns);
    }
    if (did_sigchld) continue;

    // 2. Set up IO
    vec_clear(&io.sources);
    vec_clear(&io.pollfds);
    struct io_source stdin_src = {.fd = STDIN_FILENO, .events = POLL_IN};
    vec_push(&io.sources, &stdin_src);
    struct vte_host *h;
    vec_foreach(h, app.multiplexer.hosted) {
      struct io_source src = {.fd = h->pty, .events = POLL_IN};
      vec_push(&io.sources, &src);
    }

    // 3. Dispatch all pending io
    io_flush(&io, read_callback, &app);

    // 4. Render the current multiplexer state
    render_frame(&app);
    string_flush(&app.draw_buffer, STDOUT_FILENO, nullptr);
  }

  terminal_reset();
  printf("[exited]\n");

  return 0;
}
