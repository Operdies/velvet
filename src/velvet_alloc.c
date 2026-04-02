#include "collections.h"
#include "utils.h"
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <velvet_alloc.h>

static void *libc_calloc(struct velvet_alloc *v, size_t nmemb, size_t size) {
  (void)v;
  return calloc(nmemb, size);
}
static void libc_free(struct velvet_alloc *v, void *ptr) {
  (void)v;
  free(ptr);
}

struct velvet_alloc velvet_alloc_libc = {
    .calloc = libc_calloc,
    .free = libc_free,
};

struct shmem {
  struct velvet_alloc alloc;
  int fd;
  size_t committed;
  size_t head;
};


struct block {
  /* size of this block, including the block header */
  size_t size;
  /* absolute offset of the next free block. next=0 indicates this is the tail. */
  size_t next;
};


static struct block *block_next(struct shmem *s, struct block *f);

static void block_coalesce(struct shmem *s,struct block *blk) {
  struct block *next;
coalesce:
  next = block_next(s, blk);
  if (next) {
    size_t distance = (uint8_t *)next - (uint8_t *)blk;
    if (blk->size == distance) {
      blk->size += next->size;
      blk->next = next->next;
      goto coalesce;
    }
  }
}

struct metadata {
  /* size of this block, including metadata */
  size_t size;
  /* magic byte used to detect invalid free() calls */
  uint32_t magic;
};

// ABCDEF
#define ALLOC_MAGIC 0xADABCAFE

/* Round up to next alignment boundary */
#define ALIGN_UP(x, align) (((x) + (align) - 1) & ~((align) - 1))

static struct block *get_head(struct shmem *a) {
  if (a->head == 0) return NULL;
  return (struct block *)((uint8_t *)a + a->head);
}

static size_t get_offset(struct shmem *s, struct block *b) {
  return (uint8_t*)b - (uint8_t*)s;
}

static struct block *block_next(struct shmem *s, struct block *f) {
  if (f->next) {
    return (struct block *)((uint8_t *)s + f->next);
  }
  return NULL;
}

static void block_add(struct shmem *s, struct block *new_block) {
  size_t new_offset = get_offset(s, new_block);
  struct block *prev, *next;
  prev = NULL;
  next = get_head(s);
  for (get_head(s); next; prev = next, next = block_next(s, next)) {
    if (get_offset(s, next) > new_offset) {
      break;
    }
  }

  if (prev) { /* prev < new_block */
    /* the new block was inserted between two blocks, or at the tail --
     * Update the preceding block's next pointer */
    prev->next = new_offset;
  } else { /* new_block is head */
    s->head = new_offset;
  }

  if (next) { /* new_block < next */
    new_block->next = get_offset(s, next);
  } else { /* new_block is tail */
    new_block->next = 0;
  }
}

static void *mmap_calloc(struct velvet_alloc *v, size_t nmemb, size_t size) {
  struct shmem *s = (struct shmem *)v;

  size_t alloc_size;
  if (__builtin_mul_overflow(nmemb, size, &alloc_size)) return NULL;
  size_t required = ALIGN_UP(alloc_size + sizeof(struct metadata), 16);

  struct block *blk = get_head(s), *prev = NULL;
  for (; blk; prev = blk, blk = block_next(s, blk)) {
    block_coalesce(s, blk);
    if (blk->size >= required) {
      size_t md_size;
      if (blk->size - required < ALIGN_UP(sizeof(struct block), 128)) {
        /* if the remaining capacity of this block is below some threshold value, 
         * just kill it instead of splitting. */
        md_size = blk->size;
        blk->size = 0;
        if (prev) {
          prev->next = blk->next;
        } else {
          s->head = blk->next;
        }
      } else {
        md_size = required;
        blk->size -= required;
      }

      struct metadata *md = (struct metadata*)((uint8_t*)blk + blk->size);
      memset(md, 0, md_size);
      md->magic = ALLOC_MAGIC;
      md->size = md_size;
      void *userdata = md + 1;
      return userdata;
    }
  }

  return NULL;
}

static void mmap_free(struct velvet_alloc *v, void *ptr) {
  if (ptr == NULL) return;
  struct shmem *s = (struct shmem *)v;
  struct metadata *md = (struct metadata*)ptr - 1;
  if (md->magic != ALLOC_MAGIC) velvet_die("mmap_free: magic mismatch.");

  /* insert this block in the free list. */
  struct block *new_block = (struct block*)md;
  new_block->size = md->size;
  new_block->next = 0;
  block_add(s, new_block);
}

static void *mmap_realloc(struct velvet_alloc *v, void *ptr, size_t nmemb, size_t size) {
  if (ptr) {
    struct metadata *md = (struct metadata*)ptr - 1;
    if (md->magic != ALLOC_MAGIC) velvet_die("mmap_realloc: magic mismatch.");
    size_t data_size = md->size - sizeof(*md);

    /* TODO: shrink if size reduction is significant */
    if (nmemb * size <= data_size) {
      return ptr;
    }

    /* grow */
    void *new = mmap_calloc(v, nmemb, size);
    if (!new) 
      return NULL;
    memcpy(new, ptr, data_size);
    mmap_free(v, ptr);
    return new;
  } else {
    return mmap_calloc(v, nmemb, size);
  }
}

struct velvet_alloc *velvet_alloc_shmem_create(size_t commit) {
  /* mmap must be pagesize aligned */
  commit = ALIGN_UP(commit, sysconf(_SC_PAGESIZE));
  char shm_name[100];
  snprintf(shm_name, 99, "/velvet.%d", getpid());

  int shm_fd = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR, 0600);
  if (shm_fd < 0) velvet_fatal("shm_open:");
  /* MacOS does not support O_CLOEXEC in the flags to shm_open(), so we set it with fcntl instead. It's okay if the
   * platform does not support this. We leak some file descriptors in child processes, but that shouldn't be a problem.
   * We intentionally silently ignore errors here. */
  set_cloexec(shm_fd);
  shm_unlink(shm_name);
  if (ftruncate(shm_fd, commit) == -1) velvet_fatal("truncate:");
  struct shmem *a = mmap(0, commit, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
  if (a == MAP_FAILED) velvet_fatal("mmap:");

  a->alloc.free = mmap_free;
  a->alloc.calloc = mmap_calloc;
  a->alloc.realloc = mmap_realloc;
  a->fd = shm_fd;
  a->committed = commit;
  a->head = sizeof(*a);
  struct block *head = get_head(a);
  size_t head_offset = get_offset(a, head);
  head->size = commit - head_offset;
  head->next = 0;

  return (struct velvet_alloc *)a;
}

struct velvet_alloc *velvet_alloc_shmem_remap(int fd) {
  struct stat st;
  if (fstat(fd, &st) < 0) velvet_die("fstat:");

  struct shmem *s = mmap(0, st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (s == MAP_FAILED) return NULL;
  s->fd = fd;
  return (struct velvet_alloc *)s;
}

int velvet_alloc_shmem_get_fd(struct velvet_alloc *v);
int velvet_alloc_shmem_get_fd(struct velvet_alloc *v) {
  struct shmem *sh = (struct shmem *)v;
  return sh->fd;
}

/* close() and munmap() */
void velvet_alloc_shmem_destroy(struct velvet_alloc *v, int fd) {
  struct shmem *sh = (struct shmem *)v;
  if (munmap(sh, sh->committed) != 0) velvet_die("munmap:");
  if (close(fd) != 0) velvet_die("mmap close:");
}
