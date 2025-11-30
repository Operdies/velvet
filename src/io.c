#include <io.h>
#include "utils.h"
#include <errno.h>
#include <sys/wait.h>

static bool fd_can_read(int fd) {
  const int poll_ms = 0;
  return poll(&(struct pollfd){ .fd = fd, .events = POLLIN }, 1, poll_ms) == 1;
}

void io_dispatch(struct io *io, int poll_timeout) {
  // These values are somewhat arbitrarily chosen;
  // The key idea is that we want to continually read
  // from the same file descriptor in high-throughput scenarios,
  // but we don't want the same file descriptor to indefinitely block
  // the main thread.
  constexpr int BUFSIZE = 2048;      // 2kb
  constexpr int MAX_BYTES = 1 << 19; // 512kb
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
    if (pfd->revents & POLLIN) {
      assert(src->read_callback);
      int n = 0;
      int num_reads = 0;
      do {
        n = read(pfd->fd, readbuffer, sizeof(readbuffer));
        num_reads++;
        if (n == -1) {
          // A signal was raised during the read. This is okay.
          // We can just break and read it later.
          if (errno == EINTR) break;
          // This is also ok. The fd was non-blocking was not ready for reading.
          if (errno == EAGAIN) break;
          // Log other read errors for visibility
          ERROR("read:");
        }
        struct u8_slice s = {.len = n, .content = readbuffer};
        src->read_callback(src, s);
      } while (n > 0 && num_reads < MAX_IT && fd_can_read(pfd->fd));
    }
    if (pfd->revents & POLLOUT) {
      src->write_callback(src);
    }
  }
}

void io_add_source(struct io *io, struct io_source src) {
  vec_push(&io->sources, &src);
}

void io_clear_sources(struct io *io) {
  vec_clear(&io->sources);
}

size_t io_write(struct io_source *io, struct u8_slice s) {
  size_t written = 0;
  while (s.len > written) {
    ssize_t w = write(io->fd, s.content + written, s.len - written);
    if (w == -1) {
      if (errno == EAGAIN || errno == EINTR) {
        break;
      }
      ERROR("write:");
    }
    if (w == 0) {
      break;
    }
    written += w;
  }
  return written;
}

void io_destroy(struct io *io) {
  vec_destroy(&io->pollfds);
  vec_destroy(&io->sources);
}
