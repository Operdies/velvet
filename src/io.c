#include <io.h>
#include "utils.h"
#include <errno.h>
#include <sys/wait.h>

static bool fd_hot(int fd) {
  const int poll_ms = 0;
  return poll(&(struct pollfd){ .fd = fd, .events = POLL_IN }, 1, poll_ms) == 1;
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
    // In case of unexpected errors, fail hard immediately.
    if (errno != EAGAIN && errno != EINTR) {
      die("poll %d:", errno);
    }
    // EAGAIN / EINTR are expected. In this case we should just return.
    return;
  }

  for (size_t i = 0; polled && i < io->pollfds.length; i++) {
    struct pollfd *pfd = vec_nth(&io->pollfds, i);
    struct io_source *src = vec_nth(&io->sources, i);
    assert(src->on_data);
    if (pfd->revents & POLL_IN) {
      polled--;
      int n = 0;
      int num_reads = 0;
      do {
        n = read(pfd->fd, readbuffer, sizeof(readbuffer));
        num_reads++;
        if (n == -1) {
          if (errno == EBADF) {
            // this happens if the file descriptor was closed.
            // We can safely ignore this. (Unfortunately it can also
            // be a more serious issue, but that is hard to detect.)
            // The file descriptor may be closed because a the owning process
            break;
          }
          // A signal was raised during the read. This is okay.
          // We can just break and read it later.
          if (errno == EINTR) break;
          // This is also ok. The fd was non-blocking was not ready for reading.
          if (errno == EAGAIN) break;
          // As far as I can tell this is similar to EBADF.
          // EBADF happens on MacOS, where EIO appears to be more common on Linux.
          if (errno == EIO) break;
          // Unexpected error; crash hard to make the error very visible, then determine how it should be handled.
          die("read:");
        }
        struct u8_slice s = { .len = n, .content = readbuffer };
        src->on_data(src, s);
      } while (n > 0 && num_reads < MAX_IT && fd_hot(pfd->fd));
    }
  }
}

void io_add_source(struct io *io, struct io_source src) {
  vec_push(&io->sources, &src);
}

void io_clear_sources(struct io *io) {
  vec_clear(&io->sources);
}

void io_destroy(struct io *io) {
  vec_destroy(&io->pollfds);
  vec_destroy(&io->sources);
}
