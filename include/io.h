#ifndef IO_H

#include "collections.h"
#include <poll.h>

struct io_source;
typedef void (*io_ready_callback)(struct io_source *src);
typedef void (*io_read_callback)(struct io_source *src, struct u8_slice str);
typedef void (*io_write_callback)(struct io_source *src);

enum IO_SOURCE_EVENT {
  IO_SOURCE_POLLIN = POLLIN,
  IO_SOURCE_POLLOUT = POLLOUT,
};

struct io_source {
  /* file descriptor */
  int fd;
  /* pollfd events */
  enum IO_SOURCE_EVENT events;
  /* called when data is read from the file descriptor */
  io_read_callback read_callback;
  /* called in lieu of read_callback in case the client needs to manually read */
  io_ready_callback ready_callback;
  io_write_callback write_callback;
  /* user data */
  void *data;
};

struct io_schedule {
  void (*callback)(void *data);
  void *data;
  uint64_t when;
};

struct io {
  struct vec /* io_source */ sources;
  struct vec /* pollfd */ pollfds;
  struct vec /* scheduled callbacks */ scheduled;
};

static const struct io io_default = {
    .sources = vec(struct io_source),
    .pollfds = vec(struct pollfd),
    .scheduled = vec(struct io_schedule),
};

/* Dispatch all pending io. */
void io_dispatch(struct io *io);
/* Add an io source to the io object. This source will be polled and dispatched during io_dispatch. */
void io_add_source(struct io *io, struct io_source src);
/* Remove all previously added io sources. */
void io_clear_sources(struct io *io);
/* Free all resources held by this io instance. */
void io_destroy(struct io *io);
ssize_t io_write(int fd, struct u8_slice content);
void io_schedule(struct io *io, uint64_t ms, void (*callback)(void*), void *data);

#define io_write_literal(fd, str)                                                                                        \
  io_write(fd, (struct u8_slice){.len = sizeof(str) - 1, .content = (uint8_t*)str})

#define IO_H
#endif // IO_H
