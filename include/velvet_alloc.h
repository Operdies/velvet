/* velvet_alloc.h */
#ifndef VELVET_ALLOC_H
#define VELVET_ALLOC_H

#include <stddef.h>

struct velvet_alloc {
  void* (*calloc)(struct velvet_alloc *v, size_t nmemb, size_t size) __attribute__((alloc_size(2, 3)));
  void (*free)(struct velvet_alloc *v, void  *ptr);
  void* (*realloc)(struct velvet_alloc *v, void *ptr, size_t nmemb, size_t size) __attribute__((alloc_size(3, 4)));
};

/* creates a shared memory map with shm_open(), truncates it to `commit` rounded up to pagesize, and
 * mmap()s the shared memory. The returned allocator is also the base pointer in the new memory map. */
struct velvet_alloc *velvet_alloc_shmem_create(size_t commit);
/* close() and munmap() */
void velvet_alloc_shmem_destroy(struct velvet_alloc * allocator, int fd);

/* recreate a shared memory allocator instance from a mmap file descriptor.
 * Note that this updates the internal `fd` handle in the allocator,
 * meaning the initial handle is lost.
 * */
struct velvet_alloc *velvet_alloc_shmem_remap(int fd);

int velvet_alloc_shmem_get_fd(struct velvet_alloc *v);

extern struct velvet_alloc velvet_alloc_libc;

#endif /* VELVET_ALLOC_H */

/* velvet_alloc.c */

