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

/* Dispatch all pending io.
 * If no io is pending, poll for the specified timeout. A poll timeout of -1 will suspend the process until io is
 * available. */
void io_dispatch(struct io *io, int poll_timeout);
/* Add an io source to the io object. This source will be polled and dispatched during io_dispatch. */
void io_add_source(struct io *io, struct io_source src);
/* Remove all previously added io sources. */
void io_clear_sources(struct io *io);
/* Free all resources held by this io instance. */
void io_destroy(struct io *io);

#define IO_H
#endif // IO_H
