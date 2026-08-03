#include "collections.h"
#include "utils.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* slab sizes determined based on empirical observations of lua allocation patterns. can probably be improved. */
#define SLAB_MAX (128)
static size_t slab_sizes[] = {
    48,
    64,
    96,
    SLAB_MAX,
};

#define ALLOC_INCREMENTS (1 << 16)

struct slab_entry {
  struct slab_entry *next;
  struct slab_bitmap *base;
};

struct slab_alloc {
  struct slab_entry *slabs[4];
};

struct slab_bitmap {
  uint64_t bits;
};

static struct slab_alloc *slab_new(void) {
  struct slab_alloc *ally = calloc(1, sizeof(*ally));
  return ally;
}

#define ALIGN_UP(x, align) (((x) + (align) - 1) & ~((align) - 1))

static struct slab_entry *seed_slab(size_t slab_size) {
  void *ptr;
  size_t pagesize = sysconf(_SC_PAGESIZE);
  if (posix_memalign(&ptr, pagesize, ALLOC_INCREMENTS) != 0) velvet_die("posix_memalign:");
  /* each slab takes up 1 bit in the bitmap, meaning the num_entries calculation
   * should account for each slab occupying 1 extra bit of space. */
  size_t num_entries = (8 * ALLOC_INCREMENTS / (slab_size + 1)) / 8;
  size_t bitmap_bytes = ALIGN_UP(num_entries / 8, 8);
  uint8_t *base = ((uint8_t*)ptr) + bitmap_bytes;
  for (size_t i = 0; i < num_entries - 1; i++) {
    struct slab_entry *e1 = (struct slab_entry *)(base + (i * slab_size));
    struct slab_entry *e2 = (struct slab_entry *)(base + ((i + 1) * slab_size));
    e1->next = e2;
    e2->next = NULL;
    e1->base = e2->base = ptr;
  }

  struct slab_entry *head = (void*)base;
  memset(ptr, ~0, bitmap_bytes);
  return head;
}

static int slab_get(size_t sz) {
  for (int i = 0; i < LENGTH(slab_sizes); i++) {
    if (sz <= slab_sizes[i]) {
      return i;
    }
  }
  return ~0;
}

static void *slab_malloc(struct slab_alloc *ally, size_t sz) {
  int i = slab_get(sz);
  struct slab_entry *head = ally->slabs[i];
  if (!head) {
    head = seed_slab(slab_sizes[i]);
  }
  ally->slabs[i] = head->next;
  return head;
}

static void slab_free(struct slab_alloc *ally, void *ptr, size_t sz) {
  if (ptr) {
    if (sz > SLAB_MAX) {
      free(ptr);
      return;
    }
    int i = slab_get(sz);
    struct slab_entry *e = ptr;
    struct slab_entry *head = ally->slabs[i];
    e->next = head;
    ally->slabs[i] = e;
  }
}

static void *slab_realloc(struct slab_alloc *ally, void *ptr, size_t new, size_t old) {
  void *newptr;
  /* cases:
   * 0. shrink. ignore for simplicity.
   * 1. ptr is null, we allocate new space depending on allocation size
   * 2. block fits in the slab and resize fits in the slab -> noop
   * 3. allocation is being enlarged so it no longer fits -> migrate
   * 4. allocation
   * */

  /* ignore shrinking */
  if (ptr && new == old) return ptr;

  /* if ptr is not set, this is always a fresh allocation of size `new` */
  if (!ptr) {
    if (new > SLAB_MAX) return malloc(new);
    return slab_malloc(ally, new);
  }

  /* below here, ptr is always set, so this is always a reallocation */

  /* reallocate */
  if (new <= SLAB_MAX) {
    if (slab_get(old) == slab_get(new)) 
      return ptr;
    /* realloc */
    newptr = slab_malloc(ally, new);
    memcpy(newptr, ptr, MIN(old, new));
    slab_free(ally, ptr, old);
  } else {
    if (old <= SLAB_MAX) {
      newptr = realloc(NULL, new);
      memcpy(newptr, ptr, old);
    } else {
      newptr = realloc(ptr, new);
    }
  }

  return newptr;
}
