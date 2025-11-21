#include <io.h>
#include "utils.h"
#include <errno.h>

void io_flush(struct io *io, io_callback cb, void *data) {
  static uint8_t readbuffer[4096];
  vec_clear(&io->pollfds);
  struct io_source *src;
  vec_foreach(src, io->sources) {
    struct pollfd fd = { .events = src->events, .fd = src->fd };
    vec_push(&io->pollfds, &fd);
  }

  int polled = poll(io->pollfds.content, io->pollfds.length, -1);
  if (polled == -1) {
    // In case of unexpected errors, fail hard immediately.
    if (errno != EAGAIN && errno != EINTR) {
      die("poll %d:", errno);
    }
    // EAGAIN / EINTR are expected. In this case we should just return.
    return;
  }

  for (size_t i = 0; polled && i < io->pollfds.length; i++) {
    struct pollfd *pfd = vec_nth(io->pollfds, i);
    struct io_source *src = vec_nth(io->sources, i);
    if (pfd->revents & POLL_IN) {
      polled--;
      int n = 0;
      do {
        n = read(pfd->fd, readbuffer, sizeof(readbuffer));
        if (n == -1 && errno != EINTR && errno != EAGAIN) die("read:");
        if (n >= 0) cb(readbuffer, n, src, data);
      } while (n > 0);
    }
  }
}
