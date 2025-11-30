#include <signal.h>
#include <string.h>
#include <utils.h>
#include <vte_host.h>
#include <collections.h>
#include <poll.h>
#include <io.h>
#include <multiplexer.h>
#include "platform.h"
#include "velvet_input.h"
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
  if (sigaction(SIGTERM, &sa, NULL) == -1) {
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
  struct velvet_input input_handler;
  bool quit;
  char *quit_reason;
};

static void signal_callback(struct io_source *src, struct u8_slice str) {
  struct app_context *app = src->data;
  // 1. Dispatch any pending signals
  bool did_resize = false;
  bool did_sigchld = false;
  struct int_slice signals = { .content = (int*)str.content, .n = str.len / 4 };
  for (int i = 0; i < signals.n; i++) {
    int signal = signals.content[i];
    switch (signal) {
    case SIGTERM: {
      app->quit = true;
      app->quit_reason = "shutdown signal received";
    } break;
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
    struct platform_winsize w = {0};
    platform_get_winsize(&w);
    multiplexer_resize(&app->multiplexer, w);
  }
}


static void stdin_callback(struct io_source *src, struct u8_slice str) {
  struct app_context *m = src->data;
  if (str.len == 0) {
    m->quit = true;
    return;
  }
  velvet_input_process(&m->input_handler, str);
}

static void vte_write_callback(struct io_source *src) {
  struct vte_host *vte = src->data;
  if (vte->vte.pending_input.len) {
    size_t written = io_write(src, string_as_u8_slice(&vte->vte.pending_input));
    if (written > 0) string_drop_left(&vte->vte.pending_input, written);
  }
}

static void vte_read_callback(struct io_source *src, struct u8_slice str) {
  struct vte_host *vte = src->data;
  vte_host_process_output(vte, str);
}

static void render_func(struct u8_slice str, void *context) {
  int fd = *(int*)context;
  write(fd, str.content, str.len);
}

static void add_bindir_to_path(char *arg0) {
  if (!arg0 || !*arg0) return;
  char *path_var = getenv("PATH");
  if (!path_var) return;

  struct string new_path = {0};

  bool is_abs = *arg0 == '/';

  if (!is_abs) {
    char bindir[1024] = {0};
    getcwd(bindir, sizeof(bindir) - 1);
    string_push(&new_path, (uint8_t *)bindir);
    string_push_char(&new_path, '/');
  }

  char *last_slash = strrchr(arg0, '/');
  if (last_slash)
    string_push_range(&new_path, (uint8_t*)arg0, last_slash - arg0);

  string_push_char(&new_path, ':');
  string_push(&new_path, (uint8_t*)path_var);
  string_push_char(&new_path, 0);
  setenv("PATH", (char*)new_path.content, true);
  string_destroy(&new_path);
}

static void query_features(void) {
  struct u8_slice features[] = {
    vt_synchronized_rendering_query,
  };

  for (size_t i = 0; i < LENGTH(features); i++)
    write(STDOUT_FILENO, features[i].content, features[i].len);
}

int main(int argc, char **argv) {
  struct platform_winsize ws = {0};
  add_bindir_to_path(argv[0]);
  platform_get_winsize(&ws);

  if (ws.rows == 0 || ws.colums == 0) {
    fprintf(stderr, "Error getting terminal size. Exiting.\n");
    return 1;
  }

  terminal_setup();
  query_features();
  install_signal_handlers();

  struct app_context app = {.multiplexer = multiplexer_default};
  app.input_handler = (struct velvet_input) { .m = &app.multiplexer, .prefix = ('x' & 0x1f) };
  multiplexer_resize(&app.multiplexer, ws);

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
    struct io_source stdin_src = {
        .fd = STDIN_FILENO, .events = IO_SOURCE_POLLIN, .read_callback = stdin_callback, .data = &app};
    struct io_source signal_src = {
        .fd = signal_read, .events = IO_SOURCE_POLLIN, .read_callback = signal_callback, .data = &app};
    /* NOTE: the 'h' pointer is only guaranteed to be valid until signals and stdin are processed.
     * This is because the signal handler will remove closed clients, and the stdin handler
     * processes hotkeys which can rearrange the order of the pointers.
     * */
    struct vte_host *h;
    vec_foreach(h, app.multiplexer.hosts) {
      struct io_source read_src = {
          .data = h,
          .fd = h->pty,
          .events = IO_SOURCE_POLLIN,
          .read_callback = vte_read_callback,
          .write_callback = vte_write_callback,
      };
      if (h->vte.pending_input.len) read_src.events |= IO_SOURCE_POLLOUT;
      
      io_add_source(&io, read_src);
    }

    io_add_source(&io, signal_src);
    io_add_source(&io, stdin_src);

    // Dispatch all pending io
    // TODO: if stdin_handler->state == PREFIX or stdin_handler->state == ESCAPE,
    // set a timeout of e.g. 1s and restore the state after the 
    io_dispatch(&io, -1);

    // write pending output for each client
    vec_foreach(h, app.multiplexer.hosts) {
      // TODO: perform these writes in parallel?
      string_flush(&h->vte.pending_output, h->pty, nullptr);
    }

    // quit ?
    if (app.multiplexer.hosts.length == 0 || app.quit) break;

    // arrange
    multiplexer_arrange(&app.multiplexer);

    // Render the current app state
    multiplexer_render(&app.multiplexer, render_func, &(int){STDOUT_FILENO});
  }

  if (app.multiplexer.hosts.length == 0)
    app.quit_reason = "last window closed";

  multiplexer_destroy(&app.multiplexer);
  io_destroy(&io);

  terminal_reset();
  if (app.quit_reason) {
    printf("[exited: %s]\n", app.quit_reason);
  } else {
    printf("[exited]\n");
  }

  return 0;
}
