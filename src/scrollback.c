#include "screen.h"
#include "utils.h"
#include <string.h>
#include <sys/mman.h>
#include <sys/fcntl.h>

void *scrollback_create_ring_buffer(size_t requested_size, size_t *out_ring_size) {
  size_t page = (size_t)sysconf(_SC_PAGESIZE);
  size_t size = (requested_size + page - 1) & ~(page - 1);

  if (out_ring_size) *out_ring_size = size;

  const char *name = "/velvet_scrollback";

  int fd = shm_open(name, O_CREAT | O_RDWR, 0600);
  if (fd == -1) velvet_die("shm_open:");

  shm_unlink(name);

  if (ftruncate(fd, size) == -1) velvet_die("ftruncate:");

  /* Reserve address space:
   * guard + ring + ring + guard
   */
  size_t total = size * 2 + page * 2;

  void *base = mmap(NULL, total, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
  if (base == MAP_FAILED) velvet_die("mmap reserve");

  char *ring = (char *)base + page;

  // First mapping
  if (mmap(ring, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0) == MAP_FAILED)
    velvet_die("mmap first ring:");

  // Second mapping (wrap)
  if (mmap(ring + size, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0) == MAP_FAILED)
    velvet_die("mmap second ring:");

  close(fd);
  return ring;
}

#define SIZE(x) ((offsetof(struct scrollback_header, cells) + (x)->n_cells * sizeof(struct screen_cell)))
#define NEXT(x) ((struct scrollback_header *)((char *)(x) + SIZE(x)))
#define CELLS(x) ((struct screen_cell *)((char *)(x) + offsetof(struct scrollback_header, cells)))

static void scrollback_trim_tail(struct scrollback_buffer *b) {
  struct screen_cell *cells = CELLS(b->tail);
  assert(b->tail->has_newline);
  while (b->tail->n_cells && cells[b->tail->n_cells - 1].cp.value == ' ') 
    b->tail->n_cells--;
}

static void *normalize_pointer(struct scrollback_buffer *b, void *_ptr) {
  char *ptr = (char *)_ptr;
  char *base = b->ring;
  assert(ptr >= base);
  size_t offset = ptr - base;
  offset = offset % b->ring_size;
  ptr = base + offset;
  return ptr;
}

static void push_tail(struct scrollback_buffer *b, struct scrollback_header *new_tail) {
  new_tail = normalize_pointer(b, new_tail);
  new_tail->prev = b->tail;
  new_tail->next = nullptr;
  b->tail->next = new_tail;
  b->tail = new_tail;

  if (new_tail->has_newline)
    scrollback_trim_tail(b);
}

static bool scrollback_lines_are_same(struct scrollback *s, struct scrollback_header *a, struct scrollback_header *b) {
  return normalize_pointer(&s->buffer, a) == normalize_pointer(&s->buffer, b);
}

static void assert_invariants(struct scrollback *s) {
  assert(s);
  assert(s->enabled);
  assert(s->scrollback_size > 0);
  struct scrollback_buffer *b = &s->buffer;
  if (b->head) {
    assert(b->head);
    assert(!b->head->prev);
    assert(b->tail);
    assert(!b->tail->next);
    if (!scrollback_lines_are_same(s, b->head, b->tail)) {
      assert((char*)b->tail < (b->ring + b->ring_size));
      assert(b->tail->n_cells >= 0);
      assert(b->tail->n_cells < s->scrollback_size);
      assert(b->head->next);

      /* head->next->prev should refer to the same element, but head should still point past the end of the buffer */
      // assert(b->head->next->prev == b->head);
      assert(scrollback_lines_are_same(s, b->head, b->head->next->prev));

      assert(b->tail->prev);
      assert(b->tail->prev->next == b->tail);
    }
  }

  if (!b->head) {
    assert(!b->tail);
  }
}

bool scrollback_pop(struct scrollback *s, struct screen_line *l) {
  assert_invariants(s);
  struct scrollback_buffer *b = &s->buffer;
  if (b->tail) {
    l->cells = CELLS(b->tail);
    l->eol = b->tail->n_cells;
    l->has_newline = b->tail->has_newline;
    b->tail = b->tail->prev;
    if (b->tail) {
      b->tail->next = nullptr;
    } else {
      b->tail = b->head = nullptr;
    }
    assert_invariants(s);
    return true;
  }
  assert_invariants(s);
  return false;
}

bool scrollback_peek(struct scrollback *s, struct screen_line *l) {
  assert_invariants(s);
  struct scrollback_buffer *b = &s->buffer;
  if (b->tail) {
    l->cells = CELLS(b->tail);
    l->eol = b->tail->n_cells;
    l->has_newline = b->tail->has_newline;
    assert_invariants(s);
    return true;
  }
  assert_invariants(s);
  return false;
}

void scrollback_truncate(struct scrollback *s, int new_length) {
  assert_invariants(s);

  assert(s->buffer.tail);
  assert(s->buffer.tail->n_cells > new_length);
  s->buffer.tail->n_cells = new_length;
  s->buffer.tail->has_newline = false;

  assert_invariants(s);
}

void scrollback_clear(struct scrollback *s) {
  struct scrollback_header empty = {0};
  s->_scroll_offset = 0;
  *s->buffer.head = *s->buffer.tail = empty;
}

void scrollback_destroy(struct scrollback *s) {
  if (s->buffer.ring) {
    size_t page = getpagesize();
    char *base = (char *)s->buffer.ring - page;
    if (munmap(base, s->buffer.ring_size * 2 + page * 2) == -1) {
      velvet_die("munmap:");
    }
  }
}

static size_t scrollback_bytes_free(struct scrollback *s) {
  assert_invariants(s);

  struct scrollback_buffer *b = &s->buffer;
  if (!b->ring) return 0;
  if (!b->head) return b->ring_size;
  if (b->head == b->tail) {
    ssize_t free = b->ring_size - SIZE(b->head);
    assert(free >= 0);
    return free;
  }

  char *head = (char*)b->head;
  char *tail = (char*)b->tail;
  size_t head_offset = (head - b->ring);
  size_t tail_offset = (tail - b->ring);
  if (tail_offset < head_offset) {
    ssize_t free = head_offset - tail_offset;
    ssize_t tail_size = SIZE(b->tail);
    ssize_t free2 = free - tail_size;
    assert(free2 >= 0);
    return free2;
  } else /* b->head < b->tail */ {
    ssize_t occupied = tail_offset - head_offset + SIZE(b->tail);
    ssize_t free = b->ring_size - occupied;
    assert(free >= 0);
    return free;
  }
}

/* TODO: This design is not really working
 * 1. Counting lines is cumbersome because the scrollback content must be rewrapped on render
 * in order to account for resizing.
 * 2. Scrolled output (without resizing) cannot be correctly displayed if a line has been partially evicted.
 *
 * This should probably be rewritten so we can treat the scrollback and normal grid as a single
 * [Wx(H+SB)] buffer. We can still use a circular buffer for fast streaming to the grid;
 * * scrolling to a new line now becomes an integer increment instead of swapping rows around;
 * * row swapping functions should be rewritten to copy operations, but the bigger grid buffer gives us a convenient
 * memcpy staging buffer. 
 * * row metadata could be embedded in directly in the stream similar to the current scrollback
 *
 * The downside to this solution is that resizing suddenly becomes a costly operation because
 * the whole grid must now be reinserted. */
void scrollback_push(struct scrollback *s, struct screen_line *l) {
  /* Set aside an additional discard buffer. This is neat for performance because it allows us to bulk discard lines
   * instead of spending time maintaining head entries which are about to be evicted anyway. */
  const size_t discard_buffer_size = kB(100);
  assert_invariants(s);

  struct scrollback_header empty = { 0 };
  constexpr int header_size = sizeof(struct scrollback_header);
  if (!s->buffer.ring) {
    size_t size = s->scrollback_size + discard_buffer_size;
    size_t ring_size;
    s->buffer.ring = scrollback_create_ring_buffer(size, &ring_size);
    s->buffer.ring_size = ring_size;
  }

  if (l->has_newline) {
    /* trim trailing spaces. */
    while (l->eol && l->cells[l->eol-1].cp.value == ' ') l->eol--;
  }

  struct scrollback_buffer *b = &s->buffer;
  if (!b->tail) {
    /* head and tail refer to the same memory, but length can now be obtained by b->head - b->tail */
    b->head = (struct scrollback_header*)b->ring;
    b->tail = (struct scrollback_header*)b->ring;
    *b->head = empty;
  }

  size_t bytes_required = l->eol * sizeof(struct screen_cell) + header_size;
  size_t bytes_free = scrollback_bytes_free(s);
  assert(bytes_free <= (size_t)b->ring_size);
  size_t discard_count = MAX(bytes_required, discard_buffer_size);
  while (bytes_free < discard_count) {
    assert_invariants(s);
    if ((SIZE(b->head) - header_size) > (discard_count)) {
      /* if this line is really long, partially remove it to make room for the new entry.
       * This still allows scrolling in huge output without line breaks. */
      struct scrollback_header new_head = *b->head;
      struct scrollback_header *next = b->head->next;
      size_t cells_to_remove = (discard_count + sizeof(struct screen_cell) - 1) / sizeof(struct screen_cell);
      size_t bytes_to_remove = cells_to_remove * sizeof(struct screen_cell);
      bytes_free += bytes_to_remove;
      new_head.n_cells -= cells_to_remove;
      struct scrollback_header *addr = normalize_pointer(b, ((char *)b->head) + bytes_to_remove);
      *addr = new_head;
      if (b->head == b->tail) b->tail = addr;
      b->head = addr;
      if (next) next->prev = addr;
    } else {
      struct scrollback_header *next_head = b->head->next;
      if (next_head) {
        bytes_free += SIZE(b->head);
        next_head->prev = nullptr;
        b->head = next_head;
      } else {
        bytes_free = b->ring_size;
        b->head = (struct scrollback_header *)b->ring;
        b->tail = (struct scrollback_header *)b->ring;
        *b->head = empty;
      }
    }
  }

  if (!b->tail->has_newline) {
    memcpy(NEXT(b->tail), l->cells, l->eol * sizeof(*l->cells));
    b->tail->has_newline = l->has_newline;
    b->tail->n_cells += l->eol;
  } else {
    struct scrollback_header *insert = NEXT(b->tail);
    char *insert_at = (char*)CELLS(insert);
    memcpy(insert_at, l->cells, l->eol * sizeof(*l->cells));
    insert->n_cells = l->eol;
    insert->has_newline = l->has_newline;
    push_tail(b, insert);
  }

  if (b->tail->has_newline) 
    scrollback_trim_tail(b);

  assert_invariants(s);
}
