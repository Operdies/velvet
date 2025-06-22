#include "collections.h"
#include "pane.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CSI "\x1b["
#define UP(x) CSI #x "A"
#define DOWN(x) CSI #x "B"
#define RIGHT(x) CSI #x "C"
#define LEFT(x) CSI #x "D"
#define REGION(top, bottom) CSI #top ";" #bottom "r"

static bool exit_on_failure = true;
typedef char grid_5x8[5][8];
typedef char grid_5x5[5][5];

struct chargrid {
  int rows;
  int cols;
  char cells[];
};

struct chargrid *make_chargrid(int rows, int cols, char g[rows][cols]) {
  // char *cells = calloc(rows * cols, 1);
  struct chargrid *grid = calloc(sizeof(*grid) + rows * cols, 1);
  grid->rows = rows;
  grid->cols = cols;
  for (int row = 0; row < rows; row++)
    for (int col = 0; col < cols; col++) {
      char ch = g[row][col];
      grid->cells[row * cols + col] = ch ? ch : ' ';
    }
  return grid;
}

static struct chargrid *grid_to_chargrid(const struct grid *const restrict src) {
  struct chargrid *grid = calloc(sizeof(*grid) + src->w * src->h, 1);
  grid->cols = src->w;
  grid->rows = src->h;
  for (int i0 = 0; i0 < src->h; i0++) {
    int row = (i0 + src->offset) % src->h;
    struct cell *line = &src->cells[row * src->w];
    for (int col = 0; col < src->w; col++) {
      grid->cells[i0 * src->w + col] = line[col].symbol.utf8[0];
    }
  }
  return grid;
}

static void chargrid_print_diff(struct chargrid *left, struct chargrid *right, char *leftcolor, char *rightcolor) {
  char *reset = "\x1b[39m";
  char *filled = "█";
  char *separator = "───────────────────";
  int swidth = 3;
  int boxwidth = 11;
  int align = boxwidth - left->cols;

  printf("┌%.*s┬%.*s┐\r\n", swidth * (boxwidth - 1), separator, swidth * (boxwidth - 1), separator);
  printf("│ EXPECTED │  ACTUAL  │\r\n");
  printf("├%.*s┬%.*s┼%.*s┬%.*s┤\r\n│",
         swidth * left->cols,
         separator,
         swidth * (align - 2),
         separator,
         swidth * right->cols,
         separator,
         swidth * (align - 2),
         separator);
  for (int row = 0; row < left->rows; row++) {
    for (int col = 0; col < left->cols; col++) {
      bool same = left->cells[row * left->cols + col] == right->cells[row * left->cols + col];
      char *color = same ? reset : leftcolor;
      if (same || left->cells[row * left->cols + col] != ' ')
        printf("%s%c%s", color, left->cells[row * left->cols + col], reset);
      else
        printf("%s%s%s", color, filled, reset);
    }
    printf("│%*s│", align - 2, "");
    for (int col = 0; col < right->cols; col++) {
      bool same = left->cells[row * left->cols + col] == right->cells[row * left->cols + col];
      char *color = same ? reset : rightcolor;
      if (same || right->cells[row * left->cols + col] != ' ')
        printf("%s%c%s", color, right->cells[row * left->cols + col], reset);
      else
        printf("%s%s%s", color, filled, reset);
    }

    printf("│%*s│\r\n│", align - 2, "");
  }

  // printf("\r└────────┘ └────────┘\r\n");
  // printf("\r└%.*s┴%.*s┘\r\n", swidth * (boxwidth - 1), separator, swidth * (boxwidth - 1), separator);
  printf("\r└%.*s┴%.*s┴%.*s┴%.*s┘\r\n",
         swidth * left->cols,
         separator,
         swidth * (align - 2),
         separator,
         swidth * right->cols,
         separator,
         swidth * (align - 2),
         separator);
  fflush(stdout);
}

static void print_diff(struct chargrid *a, struct chargrid *b) {
  static char *green = "\x1b[32m";
  static char *red = "\x1b[31m";

  chargrid_print_diff(a, b, red, green);
}

static int n_failures = 0;
static void fail() {
  n_failures++;
  if (exit_on_failure) exit(1);
}
static void assert_ge(int actual, int expected, const char *test_name, const char *msg) {
  if (actual > expected) return;
  printf("Assertion failed: %d > %d (%s: %s)\n", actual, expected, test_name, msg);
  fail();
}

static void assert_grid_equals(struct chargrid *expected, const struct grid *const g, const char *msg) {
  bool equal = true;
  if (expected->cols != g->w || expected->rows != g->h) {
    printf("Failed assertion: Grids are not even the same size! (%s)\n", msg);
    fail();
    return;
  }

  struct chargrid *actual = grid_to_chargrid(g);
  for (int row = 0; row < g->h; row++) {
    for (int col = 0; col < g->w; col++) {
      if (expected->cells[row * g->w + col] != actual->cells[row * g->w + col]) {
        equal = false;
      }
    }
  }
  if (!equal) {
    printf("Failed assertion: Grids are not equal! (%s)\n", msg);
    print_diff(expected, actual);
    fail();
  }
  free(actual);
}

static void test_grid_input_output(const char *const test_name, const char *const input, grid_5x8 expected1) {
  const char *reset = "\x1b[2J\x1b[1;1H";
  struct chargrid *expected = make_chargrid(5, 8, expected1);

  struct pane p = {.w = 8, .h = 5};
  struct string output = {0};
  {
    string_clear(&output);
    // 1. Write the input and verify the output
    pane_write(&p, (uint8_t *)input, strlen(input));
    pane_draw(&p, false, &output);
    assert_grid_equals(expected, p.fsm.active_grid, test_name);

    // 1.b Feed the render buffer back to the fsm and verify the output is clear
    pane_write(&p, output.content, output.len);
    string_clear(&output);
    pane_draw(&p, false, &output);
    assert_grid_equals(expected, p.fsm.active_grid, test_name);
  }
  {
    // 2. Clear the screen ensuring it is clean
    string_clear(&output);
    pane_write(&p, (uint8_t *)reset, strlen(reset));
    pane_draw(&p, false, &output);
    struct chargrid *cleared = make_chargrid(5, 8, (grid_5x8){0});
    assert_grid_equals(cleared, p.fsm.active_grid, test_name);

    assert_ge(output.len, 0, test_name, "Output should not be empty after clear!");

    // See 1.b
    pane_write(&p, output.content, output.len);
    string_clear(&output);
    pane_draw(&p, false, &output);
    assert_grid_equals(cleared, p.fsm.active_grid, test_name);

    assert_ge(output.len, 0, test_name, "Output should not be empty after clear!");
  }
  {
    // 3. Redraw the screen and verify output
    string_clear(&output);
    pane_write(&p, (uint8_t *)reset, strlen(reset));
    pane_write(&p, (uint8_t *)input, strlen(input));
    pane_draw(&p, false, &output);
    assert_grid_equals(expected, p.fsm.active_grid, test_name);

    // See 1.b
    pane_write(&p, output.content, output.len);
    string_clear(&output);
    pane_draw(&p, false, &output);
    assert_grid_equals(expected, p.fsm.active_grid, test_name);
  }
  string_destroy(&output);
  pane_destroy(&p);
}

static void
test_grid_reflow_grow(const char *const test_name, const char *const input, grid_5x5 small1, grid_5x8 large1) {
  struct chargrid *small = make_chargrid(5, 5, small1);
  struct chargrid *large = make_chargrid(5, 8, large1);

  struct pane p = {.w = 5, .h = 5};
  pane_write(&p, (uint8_t *)input, strlen(input));
  struct string output = {0};
  {
    string_clear(&output);
    pane_draw(&p, false, &output);
    assert_grid_equals(small, p.fsm.active_grid, test_name);
  }
  {
    string_clear(&output);
    pane_resize(&p, 8, 5);
    pane_draw(&p, false, &output);
    assert_grid_equals(large, p.fsm.active_grid, test_name);
  }
  {
    string_clear(&output);
    pane_resize(&p, 5, 5);
    pane_draw(&p, false, &output);
    // It is always possibly to losslessly convert back to the initial grid, so let's verify that
    assert_grid_equals(small, p.fsm.active_grid, test_name);
  }

  pane_destroy(&p);
  free(small), free(large), string_destroy(&output);
}

static void
test_grid_reflow_shrink(const char *const test_name, const char *const input, grid_5x8 large1, grid_5x5 small1) {
  struct chargrid *small = make_chargrid(5, 5, small1);
  struct chargrid *large = make_chargrid(5, 8, large1);

  struct pane p = {.w = 8, .h = 5};
  pane_write(&p, (uint8_t *)input, strlen(input));
  struct string output = {0};
  {
    string_clear(&output);
    pane_draw(&p, false, &output);
    assert_grid_equals(large, p.fsm.active_grid, test_name);
  }
  {
    string_clear(&output);
    pane_resize(&p, 5, 5);
    pane_draw(&p, false, &output);
    assert_grid_equals(small, p.fsm.active_grid, test_name);
  }
  pane_destroy(&p);
  free(small), free(large), string_destroy(&output);
}

static void test_input_output(void) {
  test_grid_input_output("single character",
                         "x",
                         (grid_5x8){
                             {"x"},
                         });
  // basic wrapping logic
  test_grid_input_output("wrapping",
                         "abcdefghijk",
                         (grid_5x8){
                             {"abcdefgh"},
                             {"ijk"},
                         });
  // move cursor to extremes and type
  test_grid_input_output("cursor movement",
                         UP(123) LEFT(123) RIGHT(1) DOWN(1) "12" RIGHT(99) DOWN(99) UP(1) LEFT(1) "3",
                         (grid_5x8){
                             {"     "},
                             {" 12 "},
                             {"     "},
                             {"      3 "},
                             {"     "},
                         });
  // line 1 scrolls out of view
  test_grid_input_output("scrolling 1",
                         "line1   line2   line3   line4   line5   l",
                         (grid_5x8){
                             {"line2"},
                             {"line3"},
                             {"line4"},
                             {"line5"},
                             {"l"},
                         });
  // line 1 and 2 scroll out of view
  test_grid_input_output("scrolling 2",
                         "line1   line2   line3   line4   line5   line6   ",
                         (grid_5x8){
                             {"line2"},
                             {"line3"},
                             {"line4"},
                             {"line5"},
                             {"line6"},
                         });
  test_grid_input_output("E test command",
                         "\x1b#8",
                         (grid_5x8){
                             {"EEEEEEEE"},
                             {"EEEEEEEE"},
                             {"EEEEEEEE"},
                             {"EEEEEEEE"},
                             {"EEEEEEEE"},
                         });
  test_grid_input_output("Clear Command",
                         "\x1b#8" CSI "2J",
                         (grid_5x8){
                             {"        "},
                             {"        "},
                             {"        "},
                             {"        "},
                             {"        "},
                         });
  test_grid_input_output(
      "Clear Command 2",
      "wwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwww" CSI "2J",
      (grid_5x8){
          {"        "},
          {"        "},
          {"        "},
          {"        "},
          {"        "},
      });
  test_grid_input_output("Off by one",
                         "AAAAAAAABBBBBBBBCCCCCCCCDDDDDDDDEEEEEEEE" CSI "0m",
                         (grid_5x8){
                             {"AAAAAAAA"},
                             {"BBBBBBBB"},
                             {"CCCCCCCC"},
                             {"DDDDDDDD"},
                             {"EEEEEEEE"},
                         });
  test_grid_input_output("Off by one 2",
                         RIGHT(99) DOWN(99) "Y" CSI "0m" UP(99) LEFT(99) "X" CSI "0m",
                         (grid_5x8){
                             {"X       "},
                             {"        "},
                             {"        "},
                             {"        "},
                             {"       Y"},
                         });
  test_grid_input_output("Normal Rendering",
                         "Hello!\r\n" CSI "K" 
                         "Second line\r\n" CSI "K"
                         "Third line\r\n" CSI "K",
                         (grid_5x8){
                             {"Second l"},
                             {"ine     "},
                             {"Third li"},
                             {"ne      "},
                         });
  test_grid_input_output("Carriage Return",
                         "Hello!!\rworld",
                         (grid_5x8){
                             {"world!!"},
                         });
}

static void test_reflow(void) {
  test_grid_reflow_grow("line lengths",
                        "AAAAA"
                        "BBBBB"
                        "CCCCC"
                        "DDDDD",
                        (grid_5x5){
                            {"AAAAA"},
                            {"BBBBB"},
                            {"CCCCC"},
                            {"DDDDD"},
                            {"     "},
                        },
                        (grid_5x8){
                            {"AAAAABBB"},
                            {"BBCCCCCD"},
                            {"DDDD    "},
                        });
  test_grid_reflow_shrink("shrink grid",
                          "AAAAAAAA"
                          "BBBBBBBB"
                          "CCCCCCCC"
                          "DDDDDDDD"
                          "EEEEEEEE",
                          (grid_5x8){
                              {"AAAAAAAA"},
                              {"BBBBBBBB"},
                              {"CCCCCCCC"},
                              {"DDDDDDDD"},
                              {"EEEEEEEE"},
                          },
                          // We are displacing 3x5 characters, so we expect losing 8 A's and 7 B's
                          (grid_5x5){
                              {"BCCCC"},
                              {"CCCCD"},
                              {"DDDDD"},
                              {"DDEEE"},
                              {"EEEEE"},
                          });
  test_grid_reflow_shrink("shrink grid",
                          "AAAAAAA\n"
                          "BB\n"
                          "DDDDDDD",
                          (grid_5x8){
                              {"AAAAAAA "},
                              {"BB      "},
                              {"DDDDDDD"},
                          },
                          (grid_5x5){
                              {"AAAAA"},
                              {"AA   "},
                              {"BB   "},
                              {"DDDDD"},
                              {"DD   "},
                          });
}

static void test_scroll_regions(void) {
  test_grid_input_output("scroll region",
                         REGION(3, 3) "Hello\r\n Hi!",
                         (grid_5x8){
                             {"     "},
                             {"     "},
                             {" Hi! "},
                             {"     "},
                             {"     "},
                         });
  test_grid_input_output("scroll region",
                         REGION(2, 4) "Hello",
                         (grid_5x8){
                             {"     "},
                             {"Hello"},
                         });
}

int main(void) {
  test_input_output();
  test_reflow();
  // test_scroll_regions();
  return n_failures;
}
