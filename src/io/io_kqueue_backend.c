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

struct kqueue_backend {
  int kqueue;
  struct vec /* io_source */ current_set;
  struct vec /* kevent */ changes;
  struct vec /* kevent */ evlist;
};

static void fd_disable_read(struct kqueue_backend *b, int fd) {
  struct kevent *ev = vec_new_element(&b->changes);
  EV_SET(ev, fd, EVFILT_READ, EV_DELETE, 0, 0, 0);
}

static void fd_disable_write(struct kqueue_backend *b, int fd) {
  struct kevent *ev = vec_new_element(&b->changes);
  EV_SET(ev, fd, EVFILT_WRITE, EV_DELETE, 0, 0, 0);
}
static void fd_enable_read(struct kqueue_backend *b, int fd) {
  struct kevent *ev = vec_new_element(&b->changes);
  EV_SET(ev, fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, 0);
}

static void fd_enable_write(struct kqueue_backend *b, int fd) {
  struct kevent *ev = vec_new_element(&b->changes);
  EV_SET(ev, fd, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, 0);
}

static void io_update_pollset(struct io *io) {
  struct kqueue_backend *backend = io->backend;
  if (!backend) {
    backend = velvet_calloc(sizeof(*backend), 1);
    backend->kqueue = kqueue();
    if (backend->kqueue == -1) velvet_die("kqueue:");
    backend->current_set = vec(struct io_source);
    backend->changes = vec(struct kevent);
    backend->evlist = vec(struct kevent);
    io->backend = backend;
  }
  vec_clear(&backend->changes);
  vec_clear(&backend->evlist);

  struct io_source *src;
  struct io_source *cur;

  /* remove file descriptors which were removed */
  vec_rforeach(cur, backend->current_set) {
    vec_find(src, io->sources, src->fd == cur->fd);
    if (!src) {
      fd_disable_read(backend, cur->fd);
      fd_disable_write(backend, cur->fd);
      vec_remove(&backend->current_set, cur);
    }
  }

  vec_foreach(src, io->sources) {
    vec_find(cur, backend->current_set, cur->fd == src->fd);
    if (!cur) {
      cur = vec_new_element(&backend->current_set);
      *cur = *src;
      if (src->events & IO_SOURCE_POLLIN) {
        fd_enable_read(backend, src->fd);
      }
      if (src->events & IO_SOURCE_POLLOUT) {
        fd_enable_write(backend, src->fd);
      }
    } else {
      bool read_enabled = cur->events & IO_SOURCE_POLLIN;
      bool should_read = src->events & IO_SOURCE_POLLIN;
      bool write_enabled = cur->events & IO_SOURCE_POLLOUT;
      bool should_write = src->events & IO_SOURCE_POLLOUT;

      if (write_enabled && !should_write) {
        fd_disable_write(backend, src->fd);
      }
      if (should_write && !write_enabled) {
        fd_enable_write(backend, src->fd);
      }
      if (read_enabled && !should_read) {
        fd_disable_read(backend, src->fd);
      }
      if (should_read && !read_enabled) {
        fd_enable_read(backend, src->fd);
      }
      *cur = *src;
    }
  }
  vec_truncate(&backend->evlist, backend->changes.length);
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

  struct kqueue_backend *backend = io->backend;

  struct timespec kevent_timeout = { 0 };
  struct timespec *kevent_timeout_ptr = nullptr;
  struct timespec timeout2 = { 0 };
  if (timeout != -1) {
    uint64_t timeout2 = timeout;
    uint64_t sec = timeout / 1000;
    timeout2 -= sec * 1000;
    uint64_t nsec = timeout2* 1e6;
    kevent_timeout.tv_nsec = nsec;
    kevent_timeout.tv_sec = sec;
    kevent_timeout_ptr = &kevent_timeout;
  }

  int total_poll = 0;
  for (int it = 0; it < 10000; it++) {
    int polled = kevent(backend->kqueue,
                        backend->changes.content,
                        backend->changes.length,
                        backend->evlist.content,
                        backend->current_set.length,
                        kevent_timeout_ptr);
    vec_clear(&backend->changes);
    kevent_timeout_ptr = &kevent_timeout;
    kevent_timeout.tv_sec = 0;
    kevent_timeout.tv_nsec = 0;

    if (polled == -1) {
      // EAGAIN / EINTR are expected. In this case we should just return.
      // For other errors, log them for visibility. although they are usually not serious.
      if (errno != EAGAIN && errno != EINTR) {
        velvet_die("kqueue:");
      }
      return 0;
    }

    total_poll += polled;

    if (total_poll == 0 && maybe_idle) {
      io_dispatch_idle_schedules(io);
      return 0;
    }

    if (!polled) return total_poll ? 1 : 0;

    for (int i = 0; i < polled; i++) {
      struct kevent *ev = vec_nth_unchecked(backend->evlist, i);
      struct io_source *src;
      vec_find(src, backend->current_set, src->fd == (int)ev->ident);
      if (!src) continue;
      // assert(src);

      if (ev->filter == EVFILT_READ) {
        if (src->on_readable) {
          src->on_readable(src);
        } else {
          int n = read(src->fd, io->buffer, sizeof(io->buffer));
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
            struct u8_slice zero = {0};
            src->on_read(src, zero);
          } else {
            struct u8_slice s = {.len = (size_t)n, .content = io->buffer};
            src->on_read(src, s);
          }
        }
      } else if (ev->filter == EVFILT_WRITE) {
        src->on_writable(src);
      }
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
  struct kqueue_backend *b = io->backend;
  if (b) {
    close(b->kqueue);
    free(b);
  }
  vec_destroy(&io->sources);
  vec_destroy(&io->scheduled_actions);
  vec_destroy(&io->idle_schedule);
}

