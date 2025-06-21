#ifndef COLLECTIONS_H
#define COLLECTIONS_H

#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define CLAMP(x, low, high) (MIN(high, MAX(x, low)))

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* TODO:
 * String prepend without copy: 
 * Write data to end of buffer
 * On resize: Cannonicalize string by copying backbuffer
 * On flush: write with iovec
 */
struct string {
  uint8_t *content;
  size_t len, cap;
};

void string_push(struct string *str, char *src, size_t len);
void string_push_char(struct string *str, char ch);
void string_memset(struct string *str, uint8_t ch, size_t len);
void string_clear(struct string *str);
void string_destroy(struct string *str);
/* flush the string instance to the specified file descriptor */
bool string_flush(struct string *str, int fd, int *total_written);


#endif /*  COLLECTIONS_H */
