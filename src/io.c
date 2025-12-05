#include <io.h>
#include "utils.h"
#include <errno.h>
#include <sys/wait.h>

#define mB(x) ((x) << 20)
#define kB(x) ((x) << 10)

void io_dispatch(struct io *io, int poll_timeout) {
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

  int polled = poll(io->pollfds.content, io->pollfds.length, poll_timeout);
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

void io_destroy(struct io *io) {
  vec_destroy(&io->pollfds);
  vec_destroy(&io->sources);
}
