#include <io.h>
#include "collections.h"
#include "platform.h"
#include "utils.h"
#include <errno.h>
#include <stdarg.h>
#include <sys/wait.h>
#include <time.h>

static void io_dispatch_schedules(struct vec v) {
  struct io_schedule *schedule;
  vec_foreach(schedule, v) {
    schedule->callback(schedule->data);
  }
}

static void io_dispatch_scheduled(struct io *io) {
  uint64_t now = get_ms_since_startup();
  struct io_schedule *s;
  vec_rforeach(s, io->scheduled_actions) {
    if (s->when > now) break;
    vec_push(&io->schedule_buffer, s);
  }
  io->scheduled_actions.length -= io->schedule_buffer.length;
  io_dispatch_schedules(io->schedule_buffer);
  vec_clear(&io->schedule_buffer);
}

static void io_dispatch_idle_schedules(struct io *io) {
  struct vec tmp = io->idle_schedule;
  io->idle_schedule = io->schedule_buffer;
  io->schedule_buffer = tmp;
  io_dispatch_schedules(io->schedule_buffer);
  vec_clear(&io->schedule_buffer);
}

void io_dispatch(struct io *io) {
  vec_clear(&io->pollfds);
  struct io_source *src;
  vec_foreach(src, io->sources) {
    struct pollfd fd = {.events = src->events, .fd = src->fd};
    vec_push(&io->pollfds, &fd);
  }

  /* TODO: Switch to epoll / kqueue based implementation */
  struct io_schedule *next_schedule = NULL;
  if (io->scheduled_actions.length > 0) next_schedule = vec_nth(io->scheduled_actions, io->scheduled_actions.length - 1);

  uint64_t now = get_ms_since_startup();
  int timeout = next_schedule ? MAX(0, (int64_t)next_schedule->when - (int64_t)now) : -1;
  if (next_schedule && timeout < 0) timeout = 0;
  bool maybe_idle = false;
  if (io->idle_schedule.length) {
    if (timeout == -1 || timeout >= io->idle_timeout_ms) {
      timeout = io->idle_timeout_ms;
      maybe_idle = true;
    }
  }

  int polled = poll(io->pollfds.content, io->pollfds.length, timeout);
  if (polled == -1) {
    // EAGAIN / EINTR are expected. In this case we should just return.
    // For other errors, log them for visibility. although they are usually not serious.
    if (errno != EAGAIN && errno != EINTR) {
      ERROR("poll:");
    }
    return;
  }

  for (size_t i = 0, remaining = polled; remaining && i < io->pollfds.length; i++) {
    struct pollfd *pfd = vec_nth(io->pollfds, i);
    struct io_source *src = vec_nth(io->sources, i);
    assert((pfd->revents & POLLNVAL) == 0);
    if (pfd->revents) remaining--;
    for (int repeats = 0; pfd->revents && repeats < io->max_iterations; repeats++) {
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
        if (src->on_hangup) src->on_hangup(src);
        else if (src->on_read) src->on_read(src, zero);
        else if (src->on_readable) src->on_readable(src);
        else if (src->on_writable) src->on_writable(src);
        break;
      }
      pfd->revents = 0;
      pfd->events = src->events;
      int poll_ret = poll(pfd, 1, 0);
      if (poll_ret < 1) break;
    }
  }

  /* dispatch all scheduled actions from before this generation */
  io_dispatch_scheduled(io);

  /* dispatch idle schedules if nothing was polled */
  if (polled == 0 && maybe_idle) {
    io_dispatch_idle_schedules(io);
  }
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
  if (written == -1 && errno != EAGAIN && errno != EINTR && errno != EPIPE)
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

static int get_schedule_id(void) {
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

/* sort the schedules such that the next-up schedule is at the last index.
 * This is on the assumption that long-lived schedules will linger in the
 * buffer for longer, and short-lived schedules will be allocated often, so they should go on the end to reduce
 * shifting. This also makes dispatch much cheaper since no shifting is needed. */
static int schedule_cmp(const void *_a, const void *_b) {
  const struct io_schedule *a = _a, *b = _b;
  return a->when > b->when ? -1 : a->when < b->when ? 1 : 0;
}

io_schedule_id io_schedule(struct io *io, uint64_t ms, void (*callback)(void*), void *data) {
  struct io_schedule schedule = { .callback = callback, .data = data, .when = get_ms_since_startup() + ms };
  schedule.id = get_schedule_id();
  int insert_at = vec_binsearch(io->scheduled_actions, &schedule, schedule_cmp);
  if (insert_at < 0) insert_at = ~insert_at;
  vec_insert(&io->scheduled_actions, insert_at, &schedule);
  return schedule.id;
}

io_schedule_id io_schedule_idle(struct io *io, void (*callback)(void*), void *data) {
  struct io_schedule schedule = { .callback = callback, .data = data };
  schedule.id = get_schedule_id();
  vec_push(&io->idle_schedule, &schedule);
  return schedule.id;
}

void io_destroy(struct io *io) {
  vec_destroy(&io->pollfds);
  vec_destroy(&io->sources);
  vec_destroy(&io->scheduled_actions);
  vec_destroy(&io->idle_schedule);
  vec_destroy(&io->schedule_buffer);
}
