#ifndef IO_H

#include "collections.h"
#include <poll.h>

struct io_source;
typedef void (*io_callback)(struct io_source *src, uint8_t *buffer, int n);

struct io_source {
  /* file descriptor */
  int fd;
  /* pollfd events */
  short events;
  /* called when data is read from the file descriptor */
  io_callback on_data;
  /* user data */
  void *data;
};

struct io {
  struct vec /* io_source */ sources;
  struct vec /* pollfd */ pollfds;
};

static const struct io io_default = {
    .sources = vec(struct io_source),
    .pollfds = vec(struct pollfd),
};

void io_flush(struct io *io, int poll_timeout);
void io_add_source(struct io *io, struct io_source src);
void io_clear_sources(struct io *io);

#define IO_H
#endif // IO_H
