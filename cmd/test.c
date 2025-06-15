#include <stdio.h>
#include <stdlib.h>
int main(void) {
  int mag = 5;
  int count = 1 << mag;
  char *str = calloc(1, count);
  for (int i = 0; i < count; i++) {
    str[i] = 'a' + (i % ('z' - 'a'));
  }
  printf("\x1b]2;%s\x07", "Hello Title!");
}
