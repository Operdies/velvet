#include <io.h>
#include "collections.h"
#include "platform.h"
#include "utils.h"
#include <errno.h>
#include <stdarg.h>
#include <sys/event.h>
#include <sys/wait.h>
#include <time.h>

static bool io_dispatch_scheduled(struct io *io) {
  struct io_schedule *schedule;
  uint64_t now = get_ms_since_startup();
  bool did_execute = false;

  // Execute all schedules which were sequenced before the current io_dispatch call
  for (;;) {
    vec_find(schedule, io->scheduled_actions, schedule->when <= now && schedule->sequence <= io->sequence);
    if (!schedule) break;
    // Create a local copy of this schedule in case `scheduled_actions` is modified during the callback.
    // This is needed because we may otherwise corrupt the vec structure.
    struct io_schedule copy = *schedule;
    vec_remove(&io->scheduled_actions, schedule);
    copy.callback(copy.data);
    did_execute = true;
  }
  return did_execute;
}

static void io_dispatch_idle_schedules(struct io *io) {
  struct io_schedule *schedule;
  for (;;) {
    vec_find(schedule, io->idle_schedule, schedule->sequence <= io->sequence);
    if (!schedule) break;
    // Create a local copy of this schedule in case `scheduled_actions` is modified during the callback.
    // This is needed because we may otherwise corrupt the vec structure.
    struct io_schedule copy = *schedule;
    vec_remove(&io->idle_schedule, schedule);
    copy.callback(copy.data);
  }
}

struct poll_backend {
  struct vec /* pollfd */ pollfds;
};

static void io_update_pollset(struct io *io) {
  struct poll_backend *backend = io->backend;
  if (!backend) {
    backend = velvet_calloc(sizeof(*backend), 1);
    backend->pollfds = vec(struct pollfd);
    io->backend = backend;
  }
  vec_clear(&backend->pollfds);
  struct io_source *src;
  vec_foreach(src, io->sources) {
    struct pollfd fd = {.events = src->events, .fd = src->fd};
    vec_push(&backend->pollfds, &fd);
  }
}

int io_dispatch(struct io *io) {
  // First execute any pending scheduled actions;
  // If a schedule was executed, return. This is needed because a scheduled
  // action can affect everything, including closing file descriptors,
  // and generally affecting all kinds of behavior.
  if (io_dispatch_scheduled(io)) return 1;
  io->sequence++;

  io_update_pollset(io);

  /* TODO: Switch to epoll / kqueue based implementation */
  struct io_schedule *next_schedule = nullptr;
  struct io_schedule *schedule;
  vec_foreach(schedule, io->scheduled_actions) {
    if (!next_schedule)
      next_schedule = schedule;
    else
      next_schedule = next_schedule->when < schedule->when ? next_schedule : schedule;
  }

  uint64_t now = get_ms_since_startup();
  int timeout = next_schedule ? next_schedule->when - now : -1;
  if (next_schedule && timeout < 0) timeout = 0;
  bool maybe_idle = false;
  if (io->idle_schedule.length) {
    if (timeout == -1 || timeout >= io->idle_timeout_ms) {
      timeout = io->idle_timeout_ms;
      maybe_idle = true;
    }
  }

  struct poll_backend *backend = io->backend;

  int polled = poll(backend->pollfds.content, backend->pollfds.length, timeout);
  if (polled == -1) {
    // EAGAIN / EINTR are expected. In this case we should just return.
    // For other errors, log them for visibility. although they are usually not serious.
    if (errno != EAGAIN && errno != EINTR) {
      ERROR("poll:");
    }
    return 0;
  }

  if (polled == 0 && maybe_idle) {
    io_dispatch_idle_schedules(io);
    return 0;
  }

  if (!polled) return 0;

  for (size_t i = 0; i < backend->pollfds.length; i++) {
    struct pollfd *pfd = vec_nth(backend->pollfds, i);
    struct io_source *src = vec_nth(io->sources, i);
    assert((pfd->revents & POLLNVAL) == 0);
    for (int repeats = 0; pfd->revents & (POLLIN | POLLOUT) && repeats < io->max_iterations; repeats++) {
      const int poll_ms = 0;
      // Read output
      if ((pfd->revents & POLLIN) && src->on_readable) {
        src->on_readable(src);
      } else if (pfd->revents & POLLIN) {
        int n = read(pfd->fd, io->buffer, sizeof(io->buffer));
        if (n == -1) {
          // A signal was raised during the read. This is okay.
          // We can just break and read it later.
          if (errno == EINTR) break;
          // This is also ok. The fd was non-blocking was not ready for reading.
          if (errno == EAGAIN) break;
          // Log other read errors for visibility
          if (errno == EIO) {
            ERROR("EIO:");
            // assume this error is non-recoverable and close.
            struct u8_slice zero = {0};
            src->on_read(src, zero);
            break;
          }
          ERROR("read:");
        } else {
          struct u8_slice s = {.len = (size_t)n, .content = io->buffer};
          src->on_read(src, s);
        }
      }
      // write input
      if (pfd->revents & POLLOUT) {
        src->on_writable(src);
      }
      if (pfd->revents & POLLHUP) {
        struct u8_slice zero = {0};
        if (src->on_read) src->on_read(src, zero);
        else if (src->on_readable) src->on_readable(src);
        else if (src->on_writable) src->on_writable(src);
        break;
      }
      pfd->revents = 0;
      int poll_ret = poll(pfd, 1, poll_ms);
      if (poll_ret < 1) break;
    }
  }
  return 1;
}

void io_add_source(struct io *io, struct io_source src) {
  vec_push(&io->sources, &src);
}

void io_clear_sources(struct io *io) {
  vec_clear(&io->sources);
}

ssize_t io_write(int fd, struct u8_slice s) {
  /* clearly a bug */
  assert(s.len < (1 << 30));
  ssize_t written = write(fd, s.content, s.len);
  if (written == -1 && errno != EAGAIN && errno != EINTR)
    velvet_die("io_write:");
  return written;
}

ssize_t io_write_format_slow(int fd, char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  ssize_t written = vdprintf(fd, fmt, ap);
  if (written == -1 && errno != EAGAIN && errno != EINTR)
    velvet_die("io_write:");
  va_end(ap);
  return written;
}

static int get_schedule_id() {
  static uint64_t token = 123;
  return token++;
}

bool io_schedule_exists(struct io *io, io_schedule_id id) {
  struct io_schedule *existing;
  vec_find(existing, io->scheduled_actions, existing->id == id);
  if (!existing) {
    vec_find(existing, io->idle_schedule, existing->id == id);
  }
  return existing;
}

bool io_schedule_cancel(struct io *io, io_schedule_id id) {
  struct io_schedule *existing;
  vec_find(existing, io->scheduled_actions, existing->id == id);
  if (existing) {
    vec_remove(&io->scheduled_actions, existing);
    return true;
  }
  vec_find(existing, io->idle_schedule, existing->id == id);
  if (existing) {
    vec_remove(&io->idle_schedule, existing);
    return true;
  }
  return false;
}

io_schedule_id io_schedule(struct io *io, uint64_t ms, void (*callback)(void*), void *data) {
  struct io_schedule schedule = { .callback = callback, .data = data, .when = get_ms_since_startup() + ms, .sequence = io->sequence };
  schedule.id = get_schedule_id();
  vec_push(&io->scheduled_actions, &schedule);
  return schedule.id;
}

io_schedule_id io_schedule_idle(struct io *io, void (*callback)(void*), void *data) {
  struct io_schedule schedule = { .callback = callback, .data = data, .sequence = io->sequence };
  schedule.id = get_schedule_id();
  vec_push(&io->idle_schedule, &schedule);
  return schedule.id;
}

void io_destroy(struct io *io) {
  struct poll_backend *b = io->backend;
  if (b) {
    vec_destroy(&b->pollfds);
    free(b);
  }
  vec_destroy(&io->sources);
  vec_destroy(&io->scheduled_actions);
  vec_destroy(&io->idle_schedule);
}

