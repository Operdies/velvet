#include <io.h>
#include "collections.h"
#include "utils.h"
#include <errno.h>
#include <sys/wait.h>
#include <time.h>

#define mB(x) ((x) << 20)
#define kB(x) ((x) << 10)

static uint64_t get_ms_since_startup(void) {
  static struct timespec initial = {0};
  struct timespec now = {0};
  if (initial.tv_sec == 0 && initial.tv_nsec == 0) {
    clock_gettime(CLOCK_MONOTONIC_RAW, &initial);
  }
  clock_gettime(CLOCK_MONOTONIC_RAW, &now);
  uint64_t ms = (now.tv_sec - initial.tv_sec) * 1000;
  if (now.tv_nsec > initial.tv_nsec) {
    ms += (now.tv_nsec - initial.tv_nsec) / 1e6;
  } else {
    ms -= (initial.tv_nsec - now.tv_nsec) / 1e6;
  }
  return ms;
}


static void io_dispatch_scheduled(struct io *io) {
  uint64_t now = get_ms_since_startup();
  struct io_schedule *schedule;
  // Execute schedules in reverse so we can safely remove them from the vector.
  vec_rforeach(schedule, io->scheduled) {
    if (schedule->when <= now) {
      schedule->callback(schedule->data);
      vec_remove(&io->scheduled, vec_index(schedule, io->scheduled));
    }
  }
}

void io_dispatch(struct io *io) {
  // These values are somewhat arbitrarily chosen;
  // The key idea is that we want to continually read
  // from the same file descriptor in high-throughput scenarios,
  // but we don't want the same file descriptor to indefinitely block
  // the main thread.
  constexpr int BUFSIZE = kB(2);      // 2kB
  constexpr int MAX_BYTES = mB(1); // 1mB
  constexpr int MAX_IT = MAX_BYTES / BUFSIZE;

  static uint8_t readbuffer[BUFSIZE];
  vec_clear(&io->pollfds);
  struct io_source *src;
  vec_foreach(src, io->sources) {
    struct pollfd fd = {.events = src->events, .fd = src->fd};
    vec_push(&io->pollfds, &fd);
  }

  io_dispatch_scheduled(io);
  struct io_schedule *next_schedule = nullptr;
  struct io_schedule *schedule;
  vec_foreach(schedule, io->scheduled) {
    if (!next_schedule) next_schedule = schedule;
    else next_schedule = next_schedule->when < schedule->when ? next_schedule : schedule;
  }
  uint64_t now = get_ms_since_startup();
  int timeout = next_schedule ? next_schedule->when - now : -1;
  if (next_schedule && timeout < 0) timeout = 0;
  int polled = poll(io->pollfds.content, io->pollfds.length, timeout);
  if (polled == -1) {
    // EAGAIN / EINTR are expected. In this case we should just return.
    // For other errors, log them for visibility. although they are usually not serious.
    if (errno != EAGAIN && errno != EINTR) {
      ERROR("poll:");
    }
    return;
  }

  for (size_t i = 0; i < io->pollfds.length; i++) {
    struct pollfd *pfd = vec_nth(&io->pollfds, i);
    struct io_source *src = vec_nth(&io->sources, i);
    assert((pfd->revents & POLLNVAL) == 0);
    for (int repeats = 0; pfd->revents & (POLLIN | POLLOUT) && repeats < MAX_IT; repeats++) {
      const int poll_ms = 0;
      // Read output
      if (pfd->revents & POLLIN && src->ready_callback) {
        src->ready_callback(src);
      } else if (pfd->revents & POLLIN) {
        int n = read(pfd->fd, readbuffer, sizeof(readbuffer));
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
            struct u8_slice s = {0};
            src->read_callback(src, s);
            break;
          }
          ERROR("read:");
        } else {
          struct u8_slice s = {.len = (size_t)n, .content = readbuffer};
          src->read_callback(src, s);
        }
      }
      // write input
      if (pfd->revents & POLLOUT) {
        src->write_callback(src);
      }
      pfd->revents = 0;
      int poll_ret = poll(pfd, 1, poll_ms);
      if (poll_ret < 1) break;
    }
  }
}

void io_add_source(struct io *io, struct io_source src) {
  vec_push(&io->sources, &src);
}

void io_clear_sources(struct io *io) {
  vec_clear(&io->sources);
}

ssize_t io_write(int fd, struct u8_slice s) {
  ssize_t written = write(fd, s.content, s.len);
  if (written == -1 && errno != EAGAIN && errno != EINTR)
    ERROR("io_write:");
  return written;
}

void io_schedule(struct io *io, uint64_t ms, void (*callback)(void*), void *data) {
  struct io_schedule schedule = { .callback = callback, .data = data, .when = get_ms_since_startup() + ms };
  vec_push(&io->scheduled, &schedule);
}

void io_destroy(struct io *io) {
  vec_destroy(&io->pollfds);
  vec_destroy(&io->sources);
  vec_destroy(&io->scheduled);
}
