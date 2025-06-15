#ifndef COLLECTIONS_H
#define COLLECTIONS_H

#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define MAX(x, y) ((x) > (y) ? (x) : (y))

#include <stddef.h>
#include <stdint.h>

struct string {
  uint8_t *content;
  size_t len, cap;
};

void string_push(struct string *str, uint8_t *bytes, size_t len);
void string_clear(struct string *str);
void string_destroy(struct string *str);


#endif /*  COLLECTIONS_H */
