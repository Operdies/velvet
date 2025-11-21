#ifndef IO_H

#include "collections.h"
#include <poll.h>

struct io_source {
  /* file descriptor */
  int fd;
  /* pollfd events */
  short events;
};

typedef void (*io_callback)(uint8_t *buffer, int n, struct io_source *src, void *data);

struct io {
  struct vec /* io_source */ sources;
  struct vec /* pollfd */ pollfds;
};

static const struct io io_default = {
    .sources = vec(struct io_source),
    .pollfds = vec(struct pollfd),
};

void io_flush(struct io *io, io_callback cb, void *data);

#define IO_H
#endif // IO_H
