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

static void render_func(const uint8_t *const buffer, size_t n, void *context) {
  int fd = *(int*)context;
  write(fd, buffer, n);
}

int main(int argc, char **argv) {
  int rows, columns;
  platform_get_winsize(&rows, &columns);

  if (rows == 0 || columns == 0) {
    fprintf(stderr, "Error getting terminal size. Exiting.\n");
    return 1;
  }

  terminal_setup();
  install_signal_handlers();

  struct app_context app = {.multiplexer = multiplexer_default};
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
    // TODO: if stdin_handler->state == PREFIX or stdin_handler->state == ESCAPE,
    // set a timeout of e.g. 1s and restore the state after the 
    io_flush(&io, -1);

    // write pending output for each client
    vec_foreach(h, app.multiplexer.clients) {
      // TODO: perform these writes in parallel?
      string_flush(&h->vte.pending_output, h->pty, nullptr);
    }

    // quit ?
    if (app.multiplexer.clients.length == 0 || app.quit) break;

    // arrange
    multiplexer_arrange(&app.multiplexer);

    // Render the current app state
    multiplexer_render(&app.multiplexer, render_func, &(int){STDOUT_FILENO});
  }

  terminal_reset();
  printf("[exited]\n");

  return 0;
}
