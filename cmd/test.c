#include "collections.h"
#include "csi.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "velvet_cmd.h"
#include "velvet_scene.h"

/* the SYM macro is not useful. It exists because of a treesitter parser bug which messes up indentation otherwise */
#define SYM(X) #X
#define CSI "\x1b["
#define CUU(x) CSI #x "A"
#define CUD(x) CSI #x "B"
#define CUF(x) CSI #x "C"
#define CUB(x) CSI #x "D"
#define CUP(x, y) CSI #x ";" #y "H"
#define DECSTBM(top, bottom) CSI #top ";" #bottom "r"
#define IL(x) CSI #x "L"
#define DL(x) CSI #x "M"
#define DCH(x) CSI #x "P"
#define ICH(x) CSI #x "@"
#define ED(x) CSI #x "J"
#define EL(x) CSI #x "K"
#define SGR(x) CSI #x "m"
#define SM(x) CSI #x "h"
#define RI "\x1bM"
#define IND "\x1b" "D"
#define SD(x) CSI #x "T"
#define SU(x) CSI #x "S"
#define ECH(x) CSI #x "X"

#define EIGHT(X) X, X, X, X, X, X, X, X
#define FIVE(X) X, X, X, X, X

static bool exit_on_failure = true;
typedef char screen_5x8[5][8];
typedef char screen_5x5[5][5];

struct dumb_screen {
  int rows;
  int cols;
  char cells[];
};

struct rect bsmall = {.w = 5, .h = 5};
struct rect blarge = {.w = 8, .h = 5};

struct dumb_screen *make_dumb_screen(int rows, int cols, char g[rows][cols]) {
  struct dumb_screen *screen = calloc(sizeof(*screen) + rows * cols, 1);
  screen->rows = rows;
  screen->cols = cols;
  for (int row = 0; row < rows; row++)
    for (int col = 0; col < cols; col++) {
      char ch = g[row][col];
      screen->cells[row * cols + col] = ch ? ch : ' ';
    }
  return screen;
}

static struct dumb_screen *screen_to_dumb_screen(const struct screen *const src) {
  struct dumb_screen *screen = calloc(sizeof(*screen) + src->w * src->h, 1);
  screen->cols = src->w;
  screen->rows = src->h;
  for (int row = 0; row < src->h; row++) {
    struct screen_line *screen_row = screen_get_line(src, row);
    for (int col = 0; col < src->w; col++) {
      screen->cells[row * src->w + col] = (char)screen_row->cells[col].cp.value;
    }
  }
  return screen;
}

static void dumb_screen_print_diff(struct dumb_screen *left, struct dumb_screen *right, char *leftcolor, char *rightcolor) {
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

static void print_diff(struct dumb_screen *a, struct dumb_screen *b) {
  static char *green = "\x1b[32m";
  static char *red = "\x1b[31m";

  dumb_screen_print_diff(a, b, red, green);
}

static int n_failures = 0;
static void fail() {
  n_failures++;
  if (exit_on_failure) __builtin_trap();
}

static void assert_ge(int actual, int expected, const char *test_name, const char *msg) {
  if (actual > expected) return;
  printf("Assertion failed: %d > %d (%s: %s)\n", actual, expected, test_name, msg);
  fail();
}

static void assert_eq(int actual, int expected, const char *test_name, const char *msg) {
  if (actual == expected) return;
  printf("Assertion failed: %d == %d (%s: %s)\n", actual, expected, test_name, msg);
  fail();
}

static void assert_screen_equals(struct dumb_screen *expected, const struct screen *const g, const char *msg) {
  bool equal = true;
  if (expected->cols != g->w || expected->rows != g->h) {
    printf("Failed assertion: screens are not even the same size! (%s)\n", msg);
    fail();
    return;
  }

  struct dumb_screen *actual = screen_to_dumb_screen(g);
  for (int row = 0; row < g->h; row++) {
    for (int col = 0; col < g->w; col++) {
      if (expected->cells[row * g->w + col] != actual->cells[row * g->w + col]) {
        equal = false;
      }
    }
  }
  if (!equal) {
    printf("Failed assertion: screens are not equal! (%s)\n", msg);
    print_diff(expected, actual);
    fail();
  }
  free(actual);
}

static void render_func(struct u8_slice s, void *context) {
}

static void test_screen_input_output(const char *const outer_test_name, const char *const input, screen_5x8 expected1) {
  char testname2[1024];
  struct dumb_screen *expected = make_dumb_screen(5, 8, expected1);

  struct velvet_scene v = velvet_scene_default;
  struct velvet_window *p = vec_new_element(&v.windows);
  p->emulator = vte_default;
  velvet_scene_resize(&v, blarge);
  velvet_window_resize(p, blarge);

  {
    // 1. Write the input and verify the output
    velvet_window_process_output(p, u8_slice_from_cstr(input));
    velvet_scene_render_damage(&v, render_func, nullptr);
    velvet_scene_render_full(&v, render_func, nullptr);
    assert_screen_equals(expected, vte_get_current_screen(&p->emulator), testname2);

    // 1.b Feed the render buffer back to the vte and verify the output is clear
    velvet_window_process_output(p, string_as_u8_slice(v.renderer.draw_buffer));
    velvet_scene_render_damage(&v, render_func, nullptr);
    velvet_scene_render_full(&v, render_func, nullptr);
    snprintf(testname2, sizeof(testname2), "%s: initial replay", outer_test_name);
    assert_screen_equals(expected, vte_get_current_screen(&p->emulator), testname2);
  }
  free(expected);
  velvet_window_destroy(p);
  velvet_scene_destroy(&v);
}

static void
test_screen_reflow_grow(const char *const test_name, const char *const input, screen_5x5 small1, screen_5x8 large1) {
  struct dumb_screen *small = make_dumb_screen(5, 5, small1);
  struct dumb_screen *large = make_dumb_screen(5, 8, large1);

  struct velvet_scene v = velvet_scene_default;
  struct velvet_window *p = vec_new_element(&v.windows);
  p->emulator = vte_default;
  velvet_scene_resize(&v, bsmall);
  velvet_window_resize(p, bsmall);

  velvet_window_process_output(p, u8_slice_from_cstr(input));
  struct string output = {0};
  {
    string_clear(&output);
    velvet_scene_render_damage(&v, render_func, nullptr);
    velvet_scene_render_full(&v, render_func, nullptr);
    assert_screen_equals(small, vte_get_current_screen(&p->emulator), test_name);
  }
  {
    string_clear(&output);
    velvet_window_resize(p, blarge);
    velvet_scene_render_damage(&v, render_func, nullptr);
    velvet_scene_render_full(&v, render_func, nullptr);
    assert_screen_equals(large, vte_get_current_screen(&p->emulator), test_name);
  }
  {
    string_clear(&output);
    velvet_window_resize(p, bsmall);
    velvet_scene_render_damage(&v, render_func, nullptr);
    velvet_scene_render_full(&v, render_func, nullptr);
    // It is always possibly to losslessly convert back to the initial screen, so let's verify that
    assert_screen_equals(small, vte_get_current_screen(&p->emulator), test_name);
  }

  velvet_window_destroy(p);
  free(small), free(large), string_destroy(&output);
}

static void
test_screen_reflow_shrink(const char *const test_name, const char *const input, screen_5x8 large1, screen_5x5 small1) {
  struct dumb_screen *small = make_dumb_screen(5, 5, small1);
  struct dumb_screen *large = make_dumb_screen(5, 8, large1);

  struct velvet_scene v = velvet_scene_default;
  struct velvet_window p = {.emulator = vte_default, .border_width = 0};
  vec_push(&v.windows, &p);
  velvet_scene_resize(&v, blarge);
  velvet_window_resize(&p, blarge);
  velvet_window_process_output(&p, u8_slice_from_cstr(input));
  {
    velvet_scene_render_damage(&v, render_func, nullptr);
    velvet_scene_render_full(&v, render_func, nullptr);
    assert_screen_equals(large, vte_get_current_screen(&p.emulator), test_name);
  }
  {
    velvet_window_resize(&p, bsmall);
    velvet_scene_render_damage(&v, render_func, nullptr);
    velvet_scene_render_full(&v, render_func, nullptr);
    assert_screen_equals(small, vte_get_current_screen(&p.emulator), test_name);
  }
  velvet_window_destroy(&p);
  free(small), free(large);
}

static void test_input_output(void) {
  test_screen_input_output("single character",
                         "x",
                         (screen_5x8){
                             {"x"},
                         });
  // basic wrapping logic
  test_screen_input_output("wrapping",
                         "abcdefghijk",
                         (screen_5x8){
                             {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h' },
                             {'i', 'j', 'k'},
                         });
  // move cursor to extremes and type
  test_screen_input_output("cursor movement",
                         CUU(123) CUB(123) CUF(1) CUD(1) "12" CUF(99) CUD(99) CUU(1) CUB(1) "3",
                         (screen_5x8){
                             {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' },
                             {' ', '1', '2', ' ', ' ', ' ', ' ', ' ' },
                             {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' },
                             {' ', ' ', ' ', ' ', ' ', ' ', '3', ' ' },
                             {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' },
                         });
  test_screen_input_output("cursor movement virtual scroll",
                         "                 "
                         "                 "
                         "                 "
                         "                 " // keep
                         CUU(123) CUB(123) CUF(1) CUD(1) "12" CUF(99) CUD(99) CUU(1) CUB(1) "3",
                         (screen_5x8){
                             {"     "},
                             {" 12 "},
                             {"     "},
                             {' ', ' ', ' ', ' ', ' ', ' ', '3', ' ' },
                             {"     "},
                         });
  // line 1 scrolls out of view
  test_screen_input_output("scrolling 1",
                         "line1   line2   line3   line4   line5   l",
                         (screen_5x8){
                             {"line2"},
                             {"line3"},
                             {"line4"},
                             {"line5"},
                             {"l"},
                         });
  // line 1 and 2 scroll out of view
  test_screen_input_output("scrolling 2",
                         "line1   line2   line3   line4   line5   line6   ",
                         (screen_5x8){
                             {"line2"},
                             {"line3"},
                             {"line4"},
                             {"line5"},
                             {"line6"},
                         });
  test_screen_input_output("E test command",
                         "\x1b#8",
                         (screen_5x8){
                             {'E', 'E', 'E', 'E', 'E', 'E', 'E', 'E' },
                             {'E', 'E', 'E', 'E', 'E', 'E', 'E', 'E' },
                             {'E', 'E', 'E', 'E', 'E', 'E', 'E', 'E' },
                             {'E', 'E', 'E', 'E', 'E', 'E', 'E', 'E' },
                             {'E', 'E', 'E', 'E', 'E', 'E', 'E', 'E' },
                         });
  test_screen_input_output("Clear Command",
                         "\x1b#8" ED(2),
                         (screen_5x8){
                             {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' },
                             {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' },
                             {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' },
                             {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' },
                             {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' },
                         });
  test_screen_input_output(
      "Clear Command 2",
      "wwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwww" ED(2),
      (screen_5x8){
          {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' },
          {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' },
          {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' },
          {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' },
          {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' },
      });
  test_screen_input_output("Off by one",
                         "AAAAAAAABBBBBBBBCCCCCCCCDDDDDDDDEEEEEEEE" SGR(0),
                         (screen_5x8){
                             {'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A' },
                             {'B', 'B', 'B', 'B', 'B', 'B', 'B', 'B' },
                             {'C', 'C', 'C', 'C', 'C', 'C', 'C', 'C' },
                             {'D', 'D', 'D', 'D', 'D', 'D', 'D', 'D' },
                             {'E', 'E', 'E', 'E', 'E', 'E', 'E', 'E' },
                         });
  test_screen_input_output("Off by one 2",
                         CUF(99) CUD(99) SYM(Y) SGR(0) CUU(99) CUB(99) "X" SGR(0),
                         (screen_5x8){
                             {'X', ' ', ' ', ' ', ' ', ' ', ' ', ' ' },
                             {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' },
                             {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' },
                             {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' },
                             {' ', ' ', ' ', ' ', ' ', ' ', ' ', 'Y' },
                         });
  test_screen_input_output("Normal Rendering",
                         "Hello!\r\n" EL(0)
                         "Second line\r\n" EL(0)
                         "Third line\r\n" EL(0),
                         (screen_5x8){
                             {'S', 'e', 'c', 'o', 'n', 'd', ' ', 'l' },
                             {'i', 'n', 'e', ' ', ' ', ' ', ' ', ' ' },
                             {'T', 'h', 'i', 'r', 'd', ' ', 'l', 'i' },
                             {'n', 'e', ' ', ' ', ' ', ' ', ' ', ' ' },
                         });
  test_screen_input_output("Carriage Return",
                         "Hello!!\rworld",
                         (screen_5x8){
                             {"world!!"},
                         });

  test_screen_input_output("Insert Lines",
                         SM(20)
                             "Line1\nLine2\nLine3\nLine4\nLine5" CUP(2, 1) IL(2),
                         (screen_5x8){
                             {"Line1"},
                             {"     "},
                             {"     "},
                             {"Line2"},
                             {"Line3"},
                         });
  test_screen_input_output("Insert Lines Virtual",
                         SM(20)
                             "Line1\nLine2\nLine3\nLine4\nLine5\nLine6\nLine7" CUP(2, 1) IL(2),
                         (screen_5x8){
                             {"Line3"},
                             {"     "},
                             {"     "},
                             {"Line4"},
                             {"Line5"},
                         });
  test_screen_input_output("Delete Lines",
                         SM(20)
                             "Line1\nLine2\nLine3\nLine4\nLine5\nLine6" CUP(2, 1) DL(2),
                         (screen_5x8){
                             {"Line2"},
                             {"Line5"},
                             {"Line6"},
                         });
  test_screen_input_output("Delete Many Lines",
                         SM(20)
                             "Line1\nLine2\nLine3\nLine4\nLine5\nLine6" CUP(2, 1) DL(10),
                         (screen_5x8){
                             {"Line2"},
                         });
  test_screen_input_output("Delete Many Lines 2",
                         SM(20)
                             "Line1\nLine2\nLine3\nLine4\nLine5\nLine6" CUP(9, 1) DL(10),
                         (screen_5x8){
                             {"Line2"},
                             {"Line3"},
                             {"Line4"},
                             {"Line5"},
                             {"     "},
                         });
  test_screen_input_output("Delete Lines All But Last",
                         SM(20)
                             "Line1\nLine2\nLine3\nLine4\nLine5\nLine6" CUP(1, 2) DL(4),
                         (screen_5x8){
                             {"Line6"},
                         });
  test_screen_input_output("Insert Lines Then Delete",
                         SM(20)
                             "Line1\nLine2\nLine3\nLine4\nLine5\nLine6" CUP(2, 1) DL(2) IL(1),
                         (screen_5x8){
                             {"Line2"},
                             {"     "},
                             {"Line5"},
                             {"Line6"},
                         });
  test_screen_input_output("Delete Lines Then Insert",
                         SM(20)
                             "Line1\nLine2\nLine3\nLine4\nLine5\nLine6" CUP(2, 1) IL(2) DL(1),
                         (screen_5x8){
                             {"Line2"},
                             {"     "},
                             {"Line3"},
                             {"Line4"},
                         });
  test_screen_input_output("Overflow screen",
                         "AAAAAAAA"
                         "BBBBBBBB"
                         "CCCCCCCC"
                         "DDDDDDDD"
                         "EEEEEEEEE",
                         (screen_5x8){
                             { 'B', 'B', 'B', 'B', 'B', 'B', 'B', 'B' },
                             {'C', 'C', 'C', 'C', 'C', 'C', 'C', 'C' },
                             {'D', 'D', 'D', 'D', 'D', 'D', 'D', 'D' },
                             {'E', 'E', 'E', 'E', 'E', 'E', 'E', 'E' },
                             {'E'},
                         });
  test_screen_input_output("Fill screen",
                         "AAAAAAAA"
                         "BBBBBBBB"
                         "CCCCCCCC"
                         "DDDDDDDD"
                         "EEEEEEEE",
                         (screen_5x8){
                             {'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A' },
                             {'B', 'B', 'B', 'B', 'B', 'B', 'B', 'B' },
                             {'C', 'C', 'C', 'C', 'C', 'C', 'C', 'C' },
                             {'D', 'D', 'D', 'D', 'D', 'D', 'D', 'D' },
                             {'E', 'E', 'E', 'E', 'E', 'E', 'E', 'E' },
                         });
}

static void test_reflow(void) {
  test_screen_reflow_grow("line lengths",
                        "AAAAA"
                        "BBBBB"
                        "CCCCC"
                        "DDDDD",
                        (screen_5x5){
                            {'A', 'A', 'A', 'A', 'A' },
                            {'B', 'B', 'B', 'B', 'B' },
                            {'C', 'C', 'C', 'C', 'C' },
                            {'D', 'D', 'D', 'D', 'D' },
                            {' ', ' ', ' ', ' ', ' ' },
                        },
                        (screen_5x8){
                            {'A', 'A', 'A', 'A', 'A', 'B', 'B', 'B' },
                            {'B', 'B', 'C', 'C', 'C', 'C', 'C', 'D' },
                            {'D', 'D', 'D', 'D', ' ', ' ', ' ', ' ' },
                        });
  test_screen_reflow_shrink("shrink screen",
                          "AAAAAAAA"
                          "BBBBBBBB"
                          "CCCCCCCC"
                          "DDDDDDDD"
                          "EEEEEEEE",
                          (screen_5x8){
                              {'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A' },
                              {'B', 'B', 'B', 'B', 'B', 'B', 'B', 'B' },
                              {'C', 'C', 'C', 'C', 'C', 'C', 'C', 'C' },
                              {'D', 'D', 'D', 'D', 'D', 'D', 'D', 'D' },
                              {'E', 'E', 'E', 'E', 'E', 'E', 'E', 'E' },
                          },
                          // We are displacing 3x5 characters, so we expect losing 8 A's and 7 B's
                          (screen_5x5){
                              {'B', 'C', 'C', 'C', 'C' },
                              {'C', 'C', 'C', 'C', 'D' },
                              {'D', 'D', 'D', 'D', 'D' },
                              {'D', 'D', 'E', 'E', 'E' },
                              {'E', 'E', 'E', 'E', 'E' },
                          });
  test_screen_reflow_shrink("shrink screen 2",
                          "AAAAAAA\r\n"
                          "BB\r\n"
                          "DDDDDDD",
                          (screen_5x8){
                              {'A', 'A', 'A', 'A', 'A', 'A', 'A', ' ' },
                              {'B', 'B', ' ', ' ', ' ', ' ', ' ', ' ' },
                              {"DDDDDDD"},
                          },
                          (screen_5x5){
                              {'A', 'A', 'A', 'A', 'A' },
                              {'A', 'A', ' ', ' ', ' ' },
                              {'B', 'B', ' ', ' ', ' ' },
                              {'D', 'D', 'D', 'D', 'D' },
                              {'D', 'D', ' ', ' ', ' ' },
                          });
}

static void test_erase(void) {
  /*
   * Test:
   * 1K: Start to cursor
   * 2K: Entire line
   * [0]K: Cursor to end
   * */
  test_screen_input_output("Line Delete",
                         "xxx" CUP(1, 2) EL(1) "\r\n"                  // Delete first two characters
                                             "xxxx" EL(2) "\r\n"       // Delete line
                                             "ababab" EL(0) "\r\n"      // Delete nothing
                                             "ababab" CUB(5) EL(0), // Delete all but first
                         (screen_5x8){
                             {' ', ' ', 'x', ' ', ' ', ' ', ' ', ' ' },
                             {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' },
                             {'a', 'b', 'a', 'b', 'a', 'b', ' ', ' ' },
                             {'a', ' ', ' ', ' ', ' ', ' ', ' ', ' ' },
                         });

  /* Test:
   * 1J: Start of screen to cursor
   * 2J: Entire screen (clear)
   * [0]J: Cursor to end of screen
   */
  test_screen_input_output("ED(1): Clear Start To Cursor (simple)", "www" ED(1), (screen_5x8){0});
  test_screen_input_output("ED(2): Clear Screen (simple)", "xxx" ED(2), (screen_5x8){0});
  test_screen_input_output("ED(0): Clear Cursor To End (simple)", "www" CUP(1, 1) ED(0), (screen_5x8){0});
  test_screen_input_output(
      "ED(1): Clear Start To Cursor", "wwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwww" ED(1), (screen_5x8){0});
  test_screen_input_output("ED(1): Clear Start To Cursor 2",
                         "wwwwwwww"
                         "wwwwwwww"
                         "wwwwwwww"
                         "wwwwwwww"
                         "wwwwwwww" CUP(5, 7) ED(1),
                         (screen_5x8){
                             {""},
                             {""},
                             {""},
                             {""},
                             {' ', ' ', ' ', ' ', ' ', ' ', ' ', 'w' },
                         });
  test_screen_input_output(
      "ED(0): Clear Cursor To End", "wwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwww" CUP(1, 1) ED(0), (screen_5x8){0});
  test_screen_input_output(
      "ED(0): Clear Cursor To End 2", "wwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwww" CUP(1, 2) ED(0), (screen_5x8){"w"});
  test_screen_input_output(
      "ED(2): Clear Screen", "wwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwww" CUP(1, 1) ED(2), (screen_5x8){0});

  test_screen_input_output("Backspace 0",
                         "w\b\bxy\b\bab",
                         (screen_5x8){
                             {"ab"},
                         });
  test_screen_input_output("Backspace 1",
                         "wwwww\b\b\bxxx\b\b\b\b\b\by",
                         (screen_5x8){
                             {"ywxxx"},
                         });
  test_screen_input_output("Insert Blanks 1",
                         "helloooooo" CUB(10) DCH(1),
                         (screen_5x8){
                             {'h', 'e', 'l', 'l', 'o', 'o', 'o', 'o' },
                             {'o', ' ', ' ', ' ', ' ', ' ', ' ', ' ' },
                         });
  test_screen_input_output("Insert Blanks 2",
                         "www" CUB(3)
                         /* delete first w */ DCH(1)
                         /* displace last w past end of line */ ICH(7),
                         (screen_5x8){
                             {' ', ' ', ' ', ' ', ' ', ' ', ' ', 'w' },
                         });
  test_screen_input_output("Line Truncated",
                         "abcd\r" ICH(2),
                         (screen_5x8){
                             {' ', ' ', 'a', 'b', 'c', 'd', ' ', ' ' },
                         });
  test_screen_input_output("ECH",
                           "wwwww" CUP(1, 2) ECH(2),
                           (screen_5x8){
                               {'w', ' ', ' ', 'w', 'w'},
                           });
}

void test_scrolling(void) {
  test_screen_input_output("Line Truncated",
                         "abcd\r" ICH(2),
                         (screen_5x8){
                             {' ', ' ', 'a', 'b', 'c', 'd', ' ', ' ' },
                         });
  test_screen_input_output("Reverse Index (RI)",
                         "\x1b#8" CUP(3,2) RI "xyz" RI RI,
                         (screen_5x8){
                         { EIGHT(' ') },
                         { EIGHT('E') },
                         { 'E', 'x', 'y', 'z', 'E', 'E', 'E', 'E' },
                         { EIGHT('E') },
                         { EIGHT('E') },
                         });
  test_screen_input_output("Index (IND)",
                         "\x1b#8" CUP(3,2) IND "xyz" IND IND IND RI "a",
                         (screen_5x8){
                         { EIGHT('E') },
                         { 'E', 'x', 'y', 'z', 'E', 'E', 'E', 'E' },
                         { EIGHT('E') },
                         { ' ', ' ', ' ', ' ', 'a', ' ', ' ', ' ' },
                         { EIGHT(' ') },
                         });
  test_screen_input_output("Scroll Down 1 (SD)",
                         "\x1b#8" SD(1),
                         (screen_5x8){
                         { EIGHT(' ') },
                         { EIGHT('E') },
                         { EIGHT('E') },
                         { EIGHT('E') },
                         { EIGHT('E') },
                         });
  test_screen_input_output("Scroll Down 2 (SD)",
                         "\x1b#8" SD(3),
                         (screen_5x8){
                         { EIGHT(' ') },
                         { EIGHT(' ') },
                         { EIGHT(' ') },
                         { EIGHT('E') },
                         { EIGHT('E') },
                         });
  test_screen_input_output("Scroll Down + Scroll Region (SD)",
                         DECSTBM(2,4) "\x1b#8" SD(2),
                         (screen_5x8){
                         { EIGHT('E') },
                         { EIGHT(' ') },
                         { EIGHT(' ') },
                         { EIGHT('E') },
                         { EIGHT('E') },
                         });
  test_screen_input_output("Scroll Up 1 (SU)",
                         "\x1b#8" SU(1),
                         (screen_5x8){
                         { EIGHT('E') },
                         { EIGHT('E') },
                         { EIGHT('E') },
                         { EIGHT('E') },
                         { EIGHT(' ') },
                         });
  test_screen_input_output("Scroll Up 2 (SU)",
                         "\x1b#8" SU(3),
                         (screen_5x8){
                         { EIGHT('E') },
                         { EIGHT('E') },
                         { EIGHT(' ') },
                         { EIGHT(' ') },
                         { EIGHT(' ') },
                         });
  test_screen_input_output("Scroll Up + Scroll Region (SU)",
                         DECSTBM(2,4) "\x1b#8" SU(2),
                         (screen_5x8){
                         { EIGHT('E') },
                         { EIGHT('E') },
                         { EIGHT(' ') },
                         { EIGHT(' ') },
                         { EIGHT('E') },
                         });
}

void assert_csi_param_equals(const char *testname, struct csi_param *expected, struct csi_param *actual, int index) {
  char indexbuf[50];
  snprintf(indexbuf, 30, "Primary %d", index);
  assert_eq(expected->primary, actual->primary, testname, indexbuf);
  for (int i = 0; i < (int)LENGTH(expected->sub); i++) {
    snprintf(indexbuf, 30, "Primary %d sub %d", index, i);
    assert_eq(expected->sub[i], actual->sub[i], testname, indexbuf);
  }
}

void assert_csi_equals(const char *testname, struct csi *expected, struct csi *actual) {
  assert_eq(expected->state, actual->state, testname, "states differ");
  assert_eq(expected->final, actual->final, testname, "final byte differs");
  assert_eq(expected->intermediate, actual->intermediate, testname, "intermediate byte differs");
  assert_eq(expected->leading, actual->leading, testname, "leading byte differs");
  assert_eq(expected->n_params, actual->n_params, testname, "n_params differs");

  for (int i = 0; i < expected->n_params; i++) {
    struct csi_param *exp = &expected->params[i];
    struct csi_param *act = &actual->params[i];
    assert_csi_param_equals(testname, exp, act, i);
  }
}

void test_csi_testcase(const char *testname, uint8_t *input, struct csi expected) {
  struct u8_slice input_slice = u8_slice_from_cstr((char *)input);
  struct csi actual = {0};
  size_t count = csi_parse(&actual, input_slice);
  if (expected.state == CSI_ACCEPT) assert(count == input_slice.len);
  assert_csi_equals(testname, &expected, &actual);
}

void test_csi_parsing(void) {
  test_csi_testcase("Reject Empty", u8"", (struct csi){.state = CSI_REJECT});
  test_csi_testcase(
      "Reset 1", u8"0m", (struct csi){.state = CSI_ACCEPT, .final = 'm', .n_params = 1, .params = {{.primary = 0}}});
  test_csi_testcase(
      "Reset 0", u8"m", (struct csi){.state = CSI_ACCEPT, .final = 'm', .n_params = 1, .params = {{.primary = 0}}});
  test_csi_testcase("Basic Parameter List",
                    u8"1;2;33;444m",
                    (struct csi){
                        .final = 'm',
                        .n_params = 4,
                        .state = CSI_ACCEPT,
                        .params = {{.primary = 1}, {.primary = 2}, {.primary = 33}, {.primary = 444}},
                    });
  test_csi_testcase(
      "RGB Modern Syntax",
      u8"38:2:100:100:100m",
      (struct csi){
          .final = 'm', .n_params = 1, .state = CSI_ACCEPT, .params = {{.primary = 38, .sub = {2, 100, 100, 100}}}});
  test_csi_testcase(
      "RGB Modern Syntax + colorspace",
      u8"38:2::100:100:100m",
      (struct csi){
          .final = 'm', .n_params = 1, .state = CSI_ACCEPT, .params = {{.primary = 38, .sub = {2, 0, 100, 100, 100}}}});
  test_csi_testcase(
      "RGB Legacy Syntax",
      u8"38;2;100;100;100m",
      (struct csi){
          .final = 'm', .n_params = 1, .state = CSI_ACCEPT, .params = {{.primary = 38, .sub = {2, 100, 100, 100}}}});
  test_csi_testcase(
      "RGB Legacy Syntax 2",
      u8"48;2;118;159;240;38;2;235;160;172m",
      (struct csi){
          .final = 'm', .n_params = 2, .state = CSI_ACCEPT, .params = {{.primary = 48, .sub = {2, 118, 159, 240}}, {.primary = 38, .sub = {2, 235, 160, 172}}}});
  test_csi_testcase(
    "Test leading / intermediate parsing 1",
    u8">c",
    (struct csi) {
      .leading = '>', .final = 'c', .state = CSI_ACCEPT, .n_params = 1 });
  test_csi_testcase(
    "Test leading / intermediate parsing 1",
    u8">?c",
    (struct csi) {
      .leading = '>', .final = 'c', .state = CSI_ACCEPT, .n_params = 1, .intermediate = '?' });
  test_csi_testcase(
    "Test leading / intermediate parsing 1",
    u8"4?c",
    (struct csi) {
      .leading = 0, .final = 'c', .state = CSI_ACCEPT, .n_params = 1, .params = {{.primary = 4}}, .intermediate = '?' });
}

#define assertf(cond, fmt, ...)                                                                                        \
  do {                                                                                                                 \
    if (!(cond)) {                                                                                                     \
      ERROR(fmt, __VA_ARGS__);                                                                                         \
      FAIL_ASSERT(cond);                                                                                               \
    }                                                                                                                  \
  } while (0)

static void test_hashmap() {
  constexpr int n_strings = 10;
  constexpr int stress = 1 << 15;

  struct hashmap h = {0};
  assert(hashmap_add(&h, 0, nullptr));
  assert(hashmap_get(&h, 0) == nullptr);
  assert(!hashmap_add(&h, 0, nullptr));
  assert(!hashmap_get(&h, 1));
  assert(hashmap_add(&h, 1, "Hello"));
  char *str;
  assert((str = hashmap_get(&h, 1)) && strcmp(str, "Hello") == 0);

  char *strings[n_strings] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
  assert(hashmap_remove(&h, 0, nullptr));
  assert(!hashmap_remove(&h, 0, nullptr));
  assert(hashmap_remove(&h, 1, nullptr));
  assert(!hashmap_remove(&h, 1, nullptr));
  for (int i = 0; i < n_strings; i++) {
    assert(hashmap_add(&h, i, strings[i]));
    for (int j = 0; j <= i; j++) {
      assert((str = hashmap_get(&h, j)));
      assert(strcmp(str, strings[j]) == 0);
    }
  }
  for (int i = 0; i < n_strings; i++) {
    assert((str = hashmap_get(&h, i)));
    assert(strcmp(str, strings[i]) == 0);
  }
  assert(h.count == n_strings);
  for (int i = 0; i < n_strings; i++) {
    assert(hashmap_remove(&h, i, nullptr));
  }
  assert(h.count == 0);

  for (int i = 0; i < stress; i++) {
    assert(hashmap_add(&h, i, strings[i % n_strings]));
    assert((str = hashmap_get(&h, i)));
    assert(strcmp(str, strings[i % n_strings]) == 0);
  }
  assert(h.count == stress);
  for (int i = 0; i < stress; i++) {
    assert((str = hashmap_get(&h, i)));
    assert(strcmp(str, strings[i % n_strings]) == 0);
  }
  assert(h.count == stress);
  for (int i = stress - 1; i >= 0; i--) {
    assert(hashmap_remove(&h, i, nullptr));
  }
  assert(h.count == 0);
  for (int i = 0; i < stress; i++) {
    assert(hashmap_add(&h, i, strings[i % n_strings]));
    assert((str = hashmap_get(&h, i)));
    assert(strcmp(str, strings[i % n_strings]) == 0);
  }
  assert(h.count == stress);
  for (int i = 0; i < stress; i += 2) {
    assert(hashmap_remove(&h, i, nullptr));
  }
  assert(h.count == stress / 2);
  for (int i = 0; i < stress; i += 2) {
    assert(!hashmap_remove(&h, i, nullptr));
  }
  assert(h.count == stress / 2);
  for (int i = 0; i < stress; i++) {
    if (i % 2 == 0) {
      assert(hashmap_add(&h, i, strings[i % n_strings]));
      assert((str = hashmap_get(&h, i)));
      assert(strcmp(str, strings[i % n_strings]) == 0);
    } else {
      assert(!hashmap_add(&h, i, strings[i % n_strings]));
    }
  }
  hashmap_destroy(&h);
}

void test_hashmap_collisions() {
  constexpr int n_strings = 10;
  char *strings[n_strings] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
  char *str;
  struct hashmap h = {0};
  for (int i = 0; i < 8; i++) {
    uint32_t k = i * 13;
    assert(hashmap_add(&h, k, strings[i]));
    assert((str = hashmap_get(&h, k)) && str == strings[i]);
  }
  for (int i = 2; i < 10; i++) {
    uint32_t k = (i % 8) * 13;
    assertf(hashmap_remove(&h, k, nullptr), "Error removing element at index %d", k);
  }
  for (int i = 0; i < 8; i++) {
    uint32_t k = i * 13;
    assert(hashmap_add(&h, k, strings[i]));
    assert((str = hashmap_get(&h, k)) && str == strings[i]);
  }
  for (int i = 7; i >= 0; i--) {
    uint32_t k = i * 13;
    assertf(hashmap_remove(&h, k, nullptr), "Error removing element at index %d", k);
  }
  hashmap_destroy(&h);
}

void test_string() {
  struct string s = {0};
  string_push(&s, u8"Hello!");
  struct u8_slice middle = string_range(&s, 1, -2);
  struct u8_slice start = string_range(&s, 0, 3);
  struct u8_slice end = string_range(&s, 2, s.len);
  assert(middle.len == 4);
  assert(middle.content[0] == 'e');
  assert(middle.content[3] == 'o');
  assert(string_starts_with(&s, start));
  assert(string_ends_with(&s, end));
  string_shift_left(&s, 1);
  assert(s.len == 5);
  assert(string_starts_with(&s, u8_slice_from_cstr("ello!")));
  string_shift_left(&s, 2);
  assert(s.len == 3);
  assert(string_starts_with(&s, u8_slice_from_cstr("lo!")));
  string_destroy(&s);
}

void test_vec() {
  int *item = nullptr;
  struct vec v = vec(int);
  assert(vec_index(&v, &item) == -1);
  vec_foreach(item, v) {
    assert(!"foreach: Vec should be empty!");
  }
  vec_rforeach(item, v) {
    assert(!"rforeach: Vec should be empty!");
  }
  vec_find(item, v, *item == 1);
  assert(item == nullptr);
  int push[] = {3, 6, 7, 8, 0, 1, 3, 5, 1};
  for (int i = 0; i < LENGTH(push); i++) {
    vec_push(&v, push + i);
  }
  int expected[] = {
      /* insert[0] */ 6, 3, 6, 7, /*insert[4] #2 */ 4, /* insert[4] */ 9, 8, 0, 1, 3, 5, 1, /* insert[length] */ 7};
  vec_insert(&v, 0, &(int){6});
  vec_insert(&v, v.length, &(int){7});
  vec_insert(&v, 4, &(int){9});
  vec_insert(&v, 4, &(int){4});
  vec_find(item, v, *item == 1);
  assert(item != nullptr);
  assert(*item == 1);
  assert(vec_index(&v, item) == 8);
  vec_find(item, v, *item == 99);
  assert(item == nullptr);
  ssize_t index = 0;
  vec_foreach(item, v) {
    int actual = *item;
    int exp = expected[index];
    int actual_index = vec_index(&v, item);
    assert(index == actual_index);
    assert(actual == exp);
    index++;
  }
  assert((size_t)index == v.length);
  index = v.length - 1;
  vec_rforeach(item, v) {
    int actual = *item;
    int exp = expected[index];
    int actual_index = vec_index(&v, item);
    assert(index == actual_index);
    assert(actual == exp);
    index--;
  }

  int where = 0;
  vec_where(item, v, *item > 3) where++;
  assert(where == 8);
  where = 0;
  vec_rwhere(item, v, *item < 3) where++;
  assert(where == 3);
  assert(index == -1);
  vec_rforeach(item, v) {
    vec_remove_at(&v, vec_index(&v, item));
  }
  vec_foreach(item, v) {
    assert(!"foreach: Vec should be empty!");
  }
  vec_rforeach(item, v) {
    assert(!"rforeach: Vec should be empty!");
  }
  vec_find(item, v, *item == 1);
  assert(item == nullptr);
  assert(v.length == 0);

  vec_set(&v, 5, &(int){7});
  assert(v.length == 6);
  assert(*(int *)vec_nth(&v, 5) == 7);
  for (int i = 0; i < 5; i++) {
    assert(*(int *)vec_nth(&v, i) == 0);
  }
  vec_set(&v, 6, &(int){9});
  assert(v.length == 7);
  assert(*(int *)vec_nth(&v, 5) == 7);
  assert(*(int *)vec_nth(&v, 6) == 9);
  for (int i = 0; i < 5; i++) {
    assert(*(int *)vec_nth(&v, i) == 0);
  }

  assert(*(int*)vec_pop(&v) == 9);
  assert(*(int*)vec_pop(&v) == 7);
  for (int i = 0; i < 5; i++) {
    assert(*(int*)vec_pop(&v) == 0);
  }
  assert(vec_pop(&v) == nullptr);
  assert(vec_pop(&v) == nullptr);

  vec_destroy(&v);
}

static void assert_u8_is(struct u8_slice s, char *str) {
  assertf(u8_slice_equals(s, u8_slice_from_cstr(str)), "Expected `%s`, was `%.*s`", str, (int)s.len, s.content);
}
void test_velvet_cmd() {
  struct velvet_cmd_iterator it;
  struct velvet_cmd_arg_iterator argit;
  struct u8_slice config = u8_slice_from_cstr("map   <C-w> 123\n"
                                              "map '<C-x>c' 'spawn zsh'"
                                              ";detach\n"
                                              ";'detach'\n"
                                              ";"
                                              "map '<C-S-f>' do something   ;"
                                              "detach\n"
                                              "map <C-x>pp notify --title hello 'ls -lah ; sleep 10' ; \n");
  it = (struct velvet_cmd_iterator){.src = config};
  {
    assert(velvet_cmd_iterator_next(&it));
    argit = (struct velvet_cmd_arg_iterator){.src = it.current};
    assert_u8_is(it.current, "map   <C-w> 123");
    char *expected[] = {"map", "<C-w>", "123"};
    for (int i = 0; i < LENGTH(expected); i++) {
      assert(velvet_cmd_arg_iterator_next(&argit));
      assert_u8_is(argit.current, expected[i]);
    }
    assert(!velvet_cmd_arg_iterator_next(&argit));
  }
  {
    assert(velvet_cmd_iterator_next(&it));
    argit = (struct velvet_cmd_arg_iterator){.src = it.current};
    assert_u8_is(it.current, "map '<C-x>c' 'spawn zsh'");
    char *expected[] = {"map", "<C-x>c", "spawn zsh"};
    for (int i = 0; i < LENGTH(expected); i++) {
      assert(velvet_cmd_arg_iterator_next(&argit));
      assert_u8_is(argit.current, expected[i]);
    }
    assert(!velvet_cmd_arg_iterator_next(&argit));
  }
  {
    assert(velvet_cmd_iterator_next(&it));
    argit = (struct velvet_cmd_arg_iterator){.src = it.current};
    assert_u8_is(it.current, "detach");
    char *expected[] = {"detach"};
    for (int i = 0; i < LENGTH(expected); i++) {
      assert(velvet_cmd_arg_iterator_next(&argit));
      assert_u8_is(argit.current, expected[i]);
    }
    assert(!velvet_cmd_arg_iterator_next(&argit));
  }
  {
    assert(velvet_cmd_iterator_next(&it));
    argit = (struct velvet_cmd_arg_iterator){.src = it.current};
    assert_u8_is(it.current, "'detach'");
    char *expected[] = {"detach"};
    for (int i = 0; i < LENGTH(expected); i++) {
      assert(velvet_cmd_arg_iterator_next(&argit));
      assert_u8_is(argit.current, expected[i]);
    }
    assert(!velvet_cmd_arg_iterator_next(&argit));
  }
  {
    assert(velvet_cmd_iterator_next(&it));
    argit = (struct velvet_cmd_arg_iterator){.src = it.current};
    assert_u8_is(it.current, "map '<C-S-f>' do something");
    char *expected[] = {"map", "<C-S-f>", "do", "something"};
    for (int i = 0; i < LENGTH(expected); i++) {
      assert(velvet_cmd_arg_iterator_next(&argit));
      assert_u8_is(argit.current, expected[i]);
    }
    assert(!velvet_cmd_arg_iterator_next(&argit));
  }
  {
    assert(velvet_cmd_iterator_next(&it));
    argit = (struct velvet_cmd_arg_iterator){.src = it.current};
    assert_u8_is(it.current, "detach");
    char *expected[] = {"detach"};
    for (int i = 0; i < LENGTH(expected); i++) {
      assert(velvet_cmd_arg_iterator_next(&argit));
      assert_u8_is(argit.current, expected[i]);
    }
    assert(!velvet_cmd_arg_iterator_next(&argit));
  }
  {
    assert(velvet_cmd_iterator_next(&it));
    argit = (struct velvet_cmd_arg_iterator){.src = it.current};
    assert_u8_is(it.current, "map <C-x>pp notify --title hello 'ls -lah ; sleep 10'");
    char *expected[] = {"map", "<C-x>pp", "notify", "--title", "hello", "ls -lah ; sleep 10"};
    for (int i = 0; i < LENGTH(expected); i++) {
      assert(velvet_cmd_arg_iterator_next(&argit));
      assert_u8_is(argit.current, expected[i]);
    }
    assert(!velvet_cmd_arg_iterator_next(&argit));
  }
  assert(!velvet_cmd_iterator_next(&it));
}

// static void assert_screen_line_equals_cstr(struct screen_line *l, char *expected) {
//   struct u8_slice ex = u8_slice_from_cstr(expected);
//   char buf[l->eol + 1];
//   for (int i = 0; i < l->eol; i++) buf[i] = (char)l->cells[i].cp.value;
//   buf[l->eol] = 0;
//   struct u8_slice actual = u8_slice_from_cstr(buf);
//   assert(u8_slice_equals(ex, actual));
// }
//
// static int cstr_to_cell_buffer(struct screen_cell *buffer, int bufsize, char *cstr) {
//   struct u8_slice slice = u8_slice_from_cstr(cstr);
//   struct u8_slice_codepoint_iterator it = {.src = slice};
//
//   int i = 0;
//   for (; i < bufsize && u8_slice_codepoint_iterator_next(&it); i++) {
//     buffer[i].cp = it.current;
//   }
//   return i;
// }

int main(void) {
  test_input_output();
  test_reflow();
  test_erase();
  test_scrolling();
  test_csi_parsing();
  test_hashmap();
  test_hashmap_collisions();
  test_string();
  test_vec();
  test_velvet_cmd();
  return n_failures;
}
