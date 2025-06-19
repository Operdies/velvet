#include "pane.h"
#include <stdio.h>
#include <stdlib.h>

#define CSI "\x1b["
#define UP(x) CSI #x "A"
#define DOWN(x) CSI #x "B"
#define RIGHT(x) CSI #x "C"
#define LEFT(x) CSI #x "D"

typedef char chargrid[5][5];

int test_cursor_move(void) {
  struct pane p = {0};
  p.w = 5;
  p.h = 5;
  char input[] = UP(123) LEFT(123) RIGHT(1) DOWN(1) "hi" RIGHT(99) DOWN(99) UP(1) LEFT(1) "x";
  chargrid expected = {
      {"     "}, // keep
      {" hi  "}, // keep
      {"     "},
      {"   x "},
      {"     "},
  };
  pane_write(&p, (uint8_t*)input, sizeof(input));
  struct string output = {0};
  string_memset(&output, ' ', sizeof(expected));
  string_clear(&output);
  pane_draw(&p, false, &output);

  // for (int row = 0; row < 5; row++) {
  //   for (int col = 0; col < 5; col++) {
  //     char ch = 
  //   }
  // }
  printf("%.*s", (int)output.len, output.content);

  pane_destroy(&p);
  return 0;
}

int main(void) {
  test_cursor_move();
}
