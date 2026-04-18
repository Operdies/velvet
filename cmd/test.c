#include "collections.h"
#include "csi.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include "velvet_alloc.h"
#include "velvet_lua.h"
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
typedef uint32_t screen_5x8[5][8];
typedef uint32_t screen_5x5[5][5];

struct dumb_screen {
  int rows;
  int cols;
  uint32_t cells[];
};

struct rect bsmall = {.width = 5, .height = 5};
struct rect blarge = {.width = 8, .height = 5};

struct dumb_screen *make_dumb_screen(int rows, int cols, uint32_t g[rows][cols]) {
  struct dumb_screen *screen = calloc(sizeof(*screen) + rows * cols * sizeof(uint32_t), 1);
  screen->rows = rows;
  screen->cols = cols;
  for (int row = 0; row < rows; row++)
    for (int col = 0; col < cols; col++) {
      uint32_t ch = g[row][col];
      screen->cells[row * cols + col] = ch ? ch : ' ';
    }
  return screen;
}

static struct dumb_screen *screen_to_dumb_screen(const struct screen *const src) {
  struct dumb_screen *screen = calloc(sizeof(*screen) + src->w * src->h * sizeof(uint32_t), 1);
  screen->cols = src->w;
  screen->rows = src->h;
  for (int row = 0; row < src->h; row++) {
    struct screen_line *screen_row = screen_get_line(src, row);
    for (int col = 0; col < src->w; col++) {
      uint32_t val = screen_row->cells[col].cp.value;
      screen->cells[row * src->w + col] = val ? val : ' ';
    }
  }
  return screen;
}

static void print_codepoint(uint32_t cp) {
  uint8_t buf[4];
  int len = codepoint_to_utf8(cp, buf);
  for (int i = 0; i < len; i++) putchar(buf[i]);
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
      if (same || left->cells[row * left->cols + col] != ' ') {
        printf("%s", color); print_codepoint(left->cells[row * left->cols + col]); printf("%s", reset);
      } else
        printf("%s%s%s", color, filled, reset);
    }
    printf("│%*s│", align - 2, "");
    for (int col = 0; col < right->cols; col++) {
      bool same = left->cells[row * left->cols + col] == right->cells[row * left->cols + col];
      char *color = same ? reset : rightcolor;
      if (same || right->cells[row * left->cols + col] != ' ') {
        printf("%s", color); print_codepoint(right->cells[row * left->cols + col]); printf("%s", reset);
      } else
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
  (void)s;
  (void)context;
}

static void test_screen_input_output(const char *const outer_test_name, const char *const input, screen_5x8 expected1) {
  char testname2[1024];
  struct dumb_screen *expected = make_dumb_screen(5, 8, expected1);

  struct velvet_scene v = velvet_scene_default;
  struct velvet_window *p = velvet_scene_manage(&v, (struct velvet_window){.emulator = vte_default});

  velvet_scene_resize(&v, blarge);
  velvet_window_resize(p, blarge, NULL);

  {
    // 1. Write the input and verify the output
    velvet_window_process_output(p, u8_slice_from_cstr(input));
    velvet_scene_render_damage(&v, render_func, NULL);
    velvet_scene_render_full(&v, render_func, NULL);
    assert_screen_equals(expected, vte_get_current_screen(&p->emulator), testname2);

    // 1.b Feed the render buffer back to the vte and verify the output is clear
    velvet_window_process_output(p, string_as_u8_slice(v.renderer.draw_buffer));
    velvet_scene_render_damage(&v, render_func, NULL);
    velvet_scene_render_full(&v, render_func, NULL);
    snprintf(testname2, sizeof(testname2), "%s: initial replay", outer_test_name);
    assert_screen_equals(expected, vte_get_current_screen(&p->emulator), testname2);
  }
  free(expected);
  velvet_scene_destroy(&v);
}

static void
test_screen_reflow_grow(const char *const test_name, const char *const input, screen_5x5 small1, screen_5x8 large1) {
  struct dumb_screen *small = make_dumb_screen(5, 5, small1);
  struct dumb_screen *large = make_dumb_screen(5, 8, large1);

  struct velvet_scene v = velvet_scene_default;
  struct velvet_window *p = velvet_scene_manage(&v, (struct velvet_window){.emulator = vte_default});
  velvet_scene_resize(&v, bsmall);
  velvet_window_resize(p, bsmall, NULL);

  velvet_window_process_output(p, u8_slice_from_cstr(input));
  struct string output = {0};
  {
    string_clear(&output);
    velvet_scene_render_damage(&v, render_func, NULL);
    velvet_scene_render_full(&v, render_func, NULL);
    assert_screen_equals(small, vte_get_current_screen(&p->emulator), test_name);
  }
  {
    string_clear(&output);
    velvet_window_resize(p, blarge, NULL);
    velvet_scene_render_damage(&v, render_func, NULL);
    velvet_scene_render_full(&v, render_func, NULL);
    assert_screen_equals(large, vte_get_current_screen(&p->emulator), test_name);
  }
  {
    string_clear(&output);
    velvet_window_resize(p, bsmall, NULL);
    velvet_scene_render_damage(&v, render_func, NULL);
    velvet_scene_render_full(&v, render_func, NULL);
    // It is always possibly to losslessly convert back to the initial screen, so let's verify that
    assert_screen_equals(small, vte_get_current_screen(&p->emulator), test_name);
  }

  velvet_scene_destroy(&v);
  free(small), free(large), string_destroy(&output);
}

static void
test_screen_reflow_shrink(const char *const test_name, const char *const input, screen_5x8 large1, screen_5x5 small1) {
  struct dumb_screen *small = make_dumb_screen(5, 5, small1);
  struct dumb_screen *large = make_dumb_screen(5, 8, large1);

  struct velvet_scene v = velvet_scene_default;
  struct velvet_window *p = velvet_scene_manage(&v, (struct velvet_window){.emulator = vte_default});
  velvet_scene_resize(&v, blarge);
  velvet_window_resize(p, blarge, NULL);
  velvet_window_process_output(p, u8_slice_from_cstr(input));
  {
    velvet_scene_render_damage(&v, render_func, NULL);
    velvet_scene_render_full(&v, render_func, NULL);
    assert_screen_equals(large, vte_get_current_screen(&p->emulator), test_name);
  }
  {
    velvet_window_resize(p, bsmall, NULL);
    velvet_scene_render_damage(&v, render_func, NULL);
    velvet_scene_render_full(&v, render_func, NULL);
    assert_screen_equals(small, vte_get_current_screen(&p->emulator), test_name);
  }
  velvet_scene_destroy(&v);
  free(small), free(large);
}

static void test_input_output(void) {
  test_screen_input_output("single character",
                         "x",
                         (screen_5x8){
                             {'x'},
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
                             {0},
                             {' ', '1', '2', ' '},
                             {0},
                             {' ', ' ', ' ', ' ', ' ', ' ', '3', ' ' },
                             {0},
                         });
  // line 1 scrolls out of view
  test_screen_input_output("scrolling 1",
                         "line1   line2   line3   line4   line5   l",
                         (screen_5x8){
                             {'l', 'i', 'n', 'e', '2'},
                             {'l', 'i', 'n', 'e', '3'},
                             {'l', 'i', 'n', 'e', '4'},
                             {'l', 'i', 'n', 'e', '5'},
                             {'l'},
                         });
  // line 1 and 2 scroll out of view
  test_screen_input_output("scrolling 2",
                         "line1   line2   line3   line4   line5   line6   ",
                         (screen_5x8){
                             {'l', 'i', 'n', 'e', '2'},
                             {'l', 'i', 'n', 'e', '3'},
                             {'l', 'i', 'n', 'e', '4'},
                             {'l', 'i', 'n', 'e', '5'},
                             {'l', 'i', 'n', 'e', '6'},
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
                             {'w', 'o', 'r', 'l', 'd', '!', '!'},
                         });

  test_screen_input_output("Insert Lines",
                         SM(20)
                             "Line1\nLine2\nLine3\nLine4\nLine5" CUP(2, 1) IL(2),
                         (screen_5x8){
                             {'L', 'i', 'n', 'e', '1'},
                             {0},
                             {0},
                             {'L', 'i', 'n', 'e', '2'},
                             {'L', 'i', 'n', 'e', '3'},
                         });
  test_screen_input_output("Insert Lines Virtual",
                         SM(20)
                             "Line1\nLine2\nLine3\nLine4\nLine5\nLine6\nLine7" CUP(2, 1) IL(2),
                         (screen_5x8){
                             {'L', 'i', 'n', 'e', '3'},
                             {0},
                             {0},
                             {'L', 'i', 'n', 'e', '4'},
                             {'L', 'i', 'n', 'e', '5'},
                         });
  test_screen_input_output("Delete Lines",
                         SM(20)
                             "Line1\nLine2\nLine3\nLine4\nLine5\nLine6" CUP(2, 1) DL(2),
                         (screen_5x8){
                             {'L', 'i', 'n', 'e', '2'},
                             {'L', 'i', 'n', 'e', '5'},
                             {'L', 'i', 'n', 'e', '6'},
                         });
  test_screen_input_output("Delete Many Lines",
                         SM(20)
                             "Line1\nLine2\nLine3\nLine4\nLine5\nLine6" CUP(2, 1) DL(10),
                         (screen_5x8){
                             {'L', 'i', 'n', 'e', '2'},
                         });
  test_screen_input_output("Delete Many Lines 2",
                         SM(20)
                             "Line1\nLine2\nLine3\nLine4\nLine5\nLine6" CUP(9, 1) DL(10),
                         (screen_5x8){
                             {'L', 'i', 'n', 'e', '2'},
                             {'L', 'i', 'n', 'e', '3'},
                             {'L', 'i', 'n', 'e', '4'},
                             {'L', 'i', 'n', 'e', '5'},
                             {0},
                         });
  test_screen_input_output("Delete Lines All But Last",
                         SM(20)
                             "Line1\nLine2\nLine3\nLine4\nLine5\nLine6" CUP(1, 2) DL(4),
                         (screen_5x8){
                             {'L', 'i', 'n', 'e', '6'},
                         });
  test_screen_input_output("Insert Lines Then Delete",
                         SM(20)
                             "Line1\nLine2\nLine3\nLine4\nLine5\nLine6" CUP(2, 1) DL(2) IL(1),
                         (screen_5x8){
                             {'L', 'i', 'n', 'e', '2'},
                             {0},
                             {'L', 'i', 'n', 'e', '5'},
                             {'L', 'i', 'n', 'e', '6'},
                         });
  test_screen_input_output("Delete Lines Then Insert",
                         SM(20)
                             "Line1\nLine2\nLine3\nLine4\nLine5\nLine6" CUP(2, 1) IL(2) DL(1),
                         (screen_5x8){
                             {'L', 'i', 'n', 'e', '2'},
                             {0},
                             {'L', 'i', 'n', 'e', '3'},
                             {'L', 'i', 'n', 'e', '4'},
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
                              {'D', 'D', 'D', 'D', 'D', 'D', 'D'},
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
                             {0},
                             {0},
                             {0},
                             {0},
                             {' ', ' ', ' ', ' ', ' ', ' ', ' ', 'w' },
                         });
  test_screen_input_output(
      "ED(0): Clear Cursor To End", "wwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwww" CUP(1, 1) ED(0), (screen_5x8){0});
  test_screen_input_output(
      "ED(0): Clear Cursor To End 2", "wwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwww" CUP(1, 2) ED(0), (screen_5x8){{'w'}});
  test_screen_input_output(
      "ED(2): Clear Screen", "wwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwww" CUP(1, 1) ED(2), (screen_5x8){0});

  test_screen_input_output("Backspace 0",
                         "w\b\bxy\b\bab",
                         (screen_5x8){
                             {'a', 'b'},
                         });
  test_screen_input_output("Backspace 1",
                         "wwwww\b\b\bxxx\b\b\b\b\b\by",
                         (screen_5x8){
                             {'y', 'w', 'x', 'x', 'x'},
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
  assert_eq(expected->prefix, actual->prefix, testname, "prefix byte differs");
  assert_eq(expected->n_params, actual->n_params, testname, "n_params differs");

  for (int i = 0; i < expected->n_params; i++) {
    struct csi_param *exp = &expected->params[i];
    struct csi_param *act = &actual->params[i];
    assert_csi_param_equals(testname, exp, act, i);
  }
}

void test_csi_testcase(const char *testname, char *input, struct csi expected) {
  struct u8_slice input_slice = u8_slice_from_cstr((char *)input);
  struct csi actual = {0};
  size_t count = csi_parse(&actual, input_slice);
  if (expected.state == CSI_ACCEPT) assert(count == input_slice.len);
  assert_csi_equals(testname, &expected, &actual);
}

void test_csi_parsing(void) {
  test_csi_testcase("Reject Empty", "", (struct csi){.state = CSI_REJECT});
  test_csi_testcase(
      "Reset 1", "0m", (struct csi){.state = CSI_ACCEPT, .final = 'm', .n_params = 1, .params = {{.primary = 0}}});
  test_csi_testcase(
      "Reset 0", "m", (struct csi){.state = CSI_ACCEPT, .final = 'm', .n_params = 0, .params = {{.primary = 0}}});
  test_csi_testcase("Basic Parameter List",
                    "1;2;33;444m",
                    (struct csi){
                        .final = 'm',
                        .n_params = 4,
                        .state = CSI_ACCEPT,
                        .params = {{.primary = 1}, {.primary = 2}, {.primary = 33}, {.primary = 444}},
                    });
  test_csi_testcase(
      "RGB Modern Syntax",
      "38:2:100:100:100m",
      (struct csi){
          .final = 'm', .n_params = 1, .state = CSI_ACCEPT, .params = {{.primary = 38, .sub = {2, 100, 100, 100}}}});
  test_csi_testcase(
      "RGB Modern Syntax + colorspace",
      "38:2::100:100:100m",
      (struct csi){
          .final = 'm', .n_params = 1, .state = CSI_ACCEPT, .params = {{.primary = 38, .sub = {2, 0, 100, 100, 100}}}});
  test_csi_testcase(
      "RGB Legacy Syntax",
      "38;2;100;100;100m",
      (struct csi){
          .final = 'm', .n_params = 1, .state = CSI_ACCEPT, .params = {{.primary = 38, .sub = {2, 100, 100, 100}}}});
  test_csi_testcase(
      "RGB Legacy Syntax 2",
      "48;2;118;159;240;38;2;235;160;172m",
      (struct csi){
          .final = 'm', .n_params = 2, .state = CSI_ACCEPT, .params = {{.primary = 48, .sub = {2, 118, 159, 240}}, {.primary = 38, .sub = {2, 235, 160, 172}}}});
  test_csi_testcase(
    "Test prefix / intermediate parsing 1",
    ">c",
    (struct csi) {
      .prefix = '>', .final = 'c', .state = CSI_ACCEPT, .n_params = 0 });
  test_csi_testcase(
    "Test prefix / intermediate parsing 1",
    ">?c",
    (struct csi) {
      .prefix = '>', .final = 'c', .state = CSI_ACCEPT, .n_params = 0, .intermediate = '?' });
  test_csi_testcase(
    "Test prefix / intermediate parsing 1",
    "4?c",
    (struct csi) {
      .prefix = 0, .final = 'c', .state = CSI_ACCEPT, .n_params = 1, .params = {{.primary = 4}}, .intermediate = '?' });
  test_csi_testcase(
    "Test n_params",
    " q",
    (struct csi) {
      .prefix = ' ', .final = 'q', .state = CSI_ACCEPT, .n_params = 0
    });
}

#define assertf(cond, fmt, ...)                                                                                        \
  do {                                                                                                                 \
    if (!(cond)) {                                                                                                     \
      ERROR(fmt, __VA_ARGS__);                                                                                         \
      FAIL_ASSERT(cond);                                                                                               \
    }                                                                                                                  \
  } while (0)

void test_string() {
  struct string s = {0};
  string_push(&s, (uint8_t *)"Hello!");
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

static void test_base64() {
  struct { const char *input; const char *expected; } cases[] = {
    { "",       "" },
    { "f",      "Zg==" },
    { "fo",     "Zm8=" },
    { "foo",    "Zm9v" },
    { "foob",   "Zm9vYg==" },
    { "fooba",  "Zm9vYmE=" },
    { "foobar", "Zm9vYmFy" },
  };
  struct string out = {0};
  for (size_t i = 0; i < LENGTH(cases); i++) {
    string_clear(&out);
    struct u8_slice in = u8_slice_from_cstr(cases[i].input);
    u8_slice_encode_base64(in, &out);
    struct u8_slice result = string_as_u8_slice(out);
    if (!u8_slice_equals(result, u8_slice_from_cstr(cases[i].expected))) {
      printf("base64 failed for '%s': got '%.*s', expected '%s'\n",
             cases[i].input, (int)result.len, result.content, cases[i].expected);
      fail();
    }
  }
  string_destroy(&out);
}

static int int_less_than(const void *_a, const void *_b) {
  int a = *(int*)_a;
  int b = *(int*)_b;
  return a - b;
}


void test_vec() {
  int *item = NULL;
  struct vec v = vec(int);
  assert(vec_index(&v, &item) == -1);
  vec_foreach(item, v) {
    assert(!"foreach: Vec should be empty!");
  }
  vec_rforeach(item, v) {
    assert(!"rforeach: Vec should be empty!");
  }
  vec_find(item, v, *item == 1);
  assert(item == NULL);
  int push[] = {3, 6, 7, 8, 0, 1, 3, 5, 1};
  for (int i = 0; i < LENGTH(push); i++) {
    vec_push(&v, push + i);
  }
  int expected[] = {
      6, 3, 6, 7,          /* insert[0] */
      4,                   /* insert[4] #2 */
      9, 8, 0, 1, 3, 5, 1, /* insert[4] */
      7,                   /* insert[length] */
  };

  vec_insert(&v, 0, &(int){6});
  vec_insert(&v, v.length, &(int){7});
  vec_insert(&v, 4, &(int){9});
  vec_insert(&v, 4, &(int){4});
  vec_find(item, v, *item == 1);
  assert(item != NULL);
  assert(*item == 1);
  assert(vec_index(&v, item) == 8);
  vec_find(item, v, *item == 99);
  assert(item == NULL);
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
  assert(item == NULL);
  assert(v.length == 0);

  vec_set(&v, 5, &(int){7});
  assert(v.length == 6);
  assert(*(int *)vec_nth(v, 5) == 7);
  for (int i = 0; i < 5; i++) {
    assert(*(int *)vec_nth(v, i) == 0);
  }
  vec_set(&v, 6, &(int){9});
  assert(v.length == 7);
  assert(*(int *)vec_nth(v, 5) == 7);
  assert(*(int *)vec_nth(v, 6) == 9);
  for (int i = 0; i < 5; i++) {
    assert(*(int *)vec_nth(v, i) == 0);
  }

  assert(*(int *)vec_pop(&v) == 9);
  assert(*(int *)vec_pop(&v) == 7);
  for (int i = 0; i < 5; i++) {
    assert(*(int *)vec_pop(&v) == 0);
  }
  assert(vec_pop(&v) == NULL);
  assert(vec_pop(&v) == NULL);


  #define V(x) &(int){x}
  vec_clear(&v);
  assert(vec_binsearch(v, V(0), int_less_than) == ~0);
  vec_push(&v, V(5));
  assert(vec_binsearch(v, V(4), int_less_than) == ~0);
  assert(vec_binsearch(v, V(5), int_less_than) == 0);
  assert(vec_binsearch(v, V(6), int_less_than) == ~1);
  vec_push(&v, V(10));
  assert(vec_binsearch(v, V(0), int_less_than) == ~0);
  assert(vec_binsearch(v, V(5), int_less_than) == 0);
  assert(vec_binsearch(v, V(7), int_less_than) == ~1);
  assert(vec_binsearch(v, V(9), int_less_than) == ~1);
  assert(vec_binsearch(v, V(10), int_less_than) == 1);
  assert(vec_binsearch(v, V(11), int_less_than) == ~2);
  vec_push(&v, V(15));
  assert(vec_binsearch(v, V(0), int_less_than) == ~0);
  assert(vec_binsearch(v, V(5), int_less_than) == 0);
  assert(vec_binsearch(v, V(7), int_less_than) == ~1);
  assert(vec_binsearch(v, V(9), int_less_than) == ~1);
  assert(vec_binsearch(v, V(10), int_less_than) == 1);
  assert(vec_binsearch(v, V(11), int_less_than) == ~2);
  assert(vec_binsearch(v, V(15), int_less_than) == 2);
  assert(vec_binsearch(v, V(17), int_less_than) == ~3);
  vec_push(&v, V(20));
  assert(vec_binsearch(v, V(0), int_less_than) == ~0);
  assert(vec_binsearch(v, V(5), int_less_than) == 0);
  assert(vec_binsearch(v, V(7), int_less_than) == ~1);
  assert(vec_binsearch(v, V(9), int_less_than) == ~1);
  assert(vec_binsearch(v, V(10), int_less_than) == 1);
  assert(vec_binsearch(v, V(11), int_less_than) == ~2);
  assert(vec_binsearch(v, V(15), int_less_than) == 2);
  assert(vec_binsearch(v, V(17), int_less_than) == ~3);
  assert(vec_binsearch(v, V(20), int_less_than) == 3);
  assert(vec_binsearch(v, V(21), int_less_than) == ~4);

  vec_destroy(&v);
}

static void test_lua();
static void test_lua_modules();

static void test_shmem_allocator() {
  size_t cap = sysconf(_SC_PAGESIZE);
  struct velvet_alloc *ally = velvet_alloc_shmem_create(cap);

  /* basic alloc returns non-null, zero-filled memory */
  char *a = ally->calloc(ally, 64, 1);
  assert(a != NULL);
  for (int i = 0; i < 64; i++) assert(a[i] == 0);

  /* multiple allocations return distinct, non-overlapping pointers */
  char *b = ally->calloc(ally, 64, 1);
  char *c = ally->calloc(ally, 64, 1);
  assert(b != NULL && c != NULL);
  assert(a != b && b != c && a != c);
  assert(abs((int)(b - a)) >= 64);
  assert(abs((int)(c - b)) >= 64);

  /* data integrity across allocations */
  memset(a, 'A', 64);
  memset(b, 'B', 64);
  memset(c, 'C', 64);
  for (int i = 0; i < 64; i++) {
    assert(a[i] == 'A');
    assert(b[i] == 'B');
    assert(c[i] == 'C');
  }

  /* free(NULL) is a no-op */
  ally->free(ally, NULL);

  /* free middle block, data in neighbors survives */
  ally->free(ally, b);
  for (int i = 0; i < 64; i++) {
    assert(a[i] == 'A');
    assert(c[i] == 'C');
  }

  /* reuse freed space */
  char *d = ally->calloc(ally, 64, 1);
  assert(d != NULL);
  for (int i = 0; i < 64; i++) assert(d[i] == 0);

  ally->free(ally, a);
  ally->free(ally, c);
  ally->free(ally, d);

  /* coalescing: after freeing everything, one big alloc should work */
  char *big = ally->calloc(ally, cap / 2, 1);
  assert(big != NULL);
  ally->free(ally, big);

  /* exhaustion: allocation larger than capacity returns NULL */
  char *too_big = ally->calloc(ally, cap, 1);
  assert(too_big == NULL);

  /* many small allocations then free all in reverse, verify coalescing recovers space */
  size_t blksz = cap / 32;
  char *ptrs[64];
  int n = 0;
  for (n = 0; n < 64; n++) {
    ptrs[n] = ally->calloc(ally, blksz, 1);
    if (!ptrs[n]) break;
    memset(ptrs[n], (char)('a' + (n % 26)), blksz);
  }
  assert(n > 0);
  for (int i = n - 1; i >= 0; i--) ally->free(ally, ptrs[i]);
  big = ally->calloc(ally, cap / 2, 1);
  assert(big != NULL);
  ally->free(ally, big);

  /* free in interleaved order to test non-adjacent coalescing */
  for (n = 0; n < 64; n++) {
    ptrs[n] = ally->calloc(ally, blksz, 1);
    if (!ptrs[n]) break;
  }
  for (int i = 0; i < n; i += 2) ally->free(ally, ptrs[i]);
  for (int i = 1; i < n; i += 2) ally->free(ally, ptrs[i]);
  big = ally->calloc(ally, cap / 2, 1);
  assert(big != NULL);
  ally->free(ally, big);

  /* non-adjacent free blocks must not coalesce: exhaust the allocator into
   * uniform blocks, free alternating ones, then request more than any single
   * gap — must fail because the free blocks are separated by live allocations */
  {
    char *e[64];
    int ne = 0;
    for (ne = 0; ne < 64; ne++) {
      e[ne] = ally->calloc(ally, blksz, 1);
      if (!e[ne]) break;
    }
    assert(ne >= 4);
    /* measure actual stride to derive a request that can't fit in one gap */
    size_t stride = (size_t)(e[0] - e[1]);
    /* free every other block */
    for (int i = 1; i < ne; i += 2) ally->free(ally, e[i]);
    /* each freed block is ~stride bytes. the leftover at the front can coalesce
     * with one adjacent freed block to form at most ~2*stride - 1 bytes.
     * requesting 2*stride guarantees it won't fit in any contiguous gap. */
    char *nope = ally->calloc(ally, 2 * stride, 1);
    assert(nope == NULL);
    /* clean up */
    for (int i = 0; i < ne; i += 2) ally->free(ally, e[i]);
  }

  /* realloc: NULL ptr acts as calloc */
  char *r1 = ally->realloc(ally, NULL, 32, 1);
  assert(r1 != NULL);
  for (int i = 0; i < 32; i++) assert(r1[i] == 0);

  /* realloc: grow copies data */
  memcpy(r1, "hello world!", 12);
  char *r2 = ally->realloc(ally, r1, 128, 1);
  assert(r2 != NULL);
  assert(memcmp(r2, "hello world!", 12) == 0);

  /* realloc: shrink preserves data */
  char *r3 = ally->realloc(ally, r2, 16, 1);
  assert(r3 != NULL);
  assert(memcmp(r3, "hello world!", 12) == 0);
  ally->free(ally, r3);

  /* nmemb * size overflow returns NULL */
  char *overflow = ally->calloc(ally, (size_t)-1, 2);
  assert(overflow == NULL);

  int fd = velvet_alloc_shmem_get_fd(ally);
  /* remap: data visible through second mapping */
  {
    int fd2 = dup(fd);
    assert(fd2 >= 0);
    struct velvet_alloc *ally2 = velvet_alloc_shmem_remap(fd2);
    assert(ally2 != NULL);

    char *s1 = ally->calloc(ally, 64, 1);
    assert(s1 != NULL);
    memcpy(s1, "shared!", 7);

    size_t offset = (uint8_t *)s1 - (uint8_t *)ally;
    char *s1_via_remap = (char *)((uint8_t *)ally2 + offset);
    assert(memcmp(s1_via_remap, "shared!", 7) == 0);

    velvet_alloc_shmem_destroy(ally2, fd2);
  }

  /* remap: data visible through third mapping */
  {
    int fd2 = dup(fd);
    assert(fd2 >= 0);
    struct velvet_alloc * ally3 = velvet_alloc_shmem_remap(fd2);
    assert( ally3 != NULL);

    char *s1 = ally->calloc(ally, 64, 1);
    assert(s1 != NULL);
    memcpy(s1, "shared!", 7);

    size_t offset = (uint8_t *)s1 - (uint8_t *)ally;
    char *s1_via_remap = (char *)((uint8_t *) ally3 + offset);
    assert(memcmp(s1_via_remap, "shared!", 7) == 0);

    velvet_alloc_shmem_destroy(ally3, fd2);
  }

  velvet_alloc_shmem_destroy(ally, fd);
}

static void test_bitmap() {
  struct tabstop_bitmap bm = {0};
  bm.bits[0] = 0b1001000011;
  bm.bits[1] = 0b00010001;


  /* tabstop_next: */
  assert(tabstop_bitmap_next(bm, 0) == 0);
  assert(tabstop_bitmap_next(bm, 1) == 1);
  assert(tabstop_bitmap_next(bm, 2) == 6);
  assert(tabstop_bitmap_next(bm, 3) == 6);
  assert(tabstop_bitmap_next(bm, 6) == 6);
  assert(tabstop_bitmap_next(bm, 7) == 9);
  assert(tabstop_bitmap_next(bm, 9) == 9);
  assert(tabstop_bitmap_next(bm, 10) == 64);
  assert(tabstop_bitmap_next(bm, 63) == 64);
  assert(tabstop_bitmap_next(bm, 64) == 64);
  assert(tabstop_bitmap_next(bm, 65) == 68);
  assert(tabstop_bitmap_next(bm, 69) == -1);

  /* tabstop_prev: */
  assert(tabstop_bitmap_prev(bm, 0) == 0);
  assert(tabstop_bitmap_prev(bm, 1) == 1);
  assert(tabstop_bitmap_prev(bm, 2) == 1);
  assert(tabstop_bitmap_prev(bm, 3) == 1);
  assert(tabstop_bitmap_prev(bm, 6) == 6);
  assert(tabstop_bitmap_prev(bm, 7) == 6);
  assert(tabstop_bitmap_prev(bm, 9) == 9);
  assert(tabstop_bitmap_prev(bm, 10) == 9);
  assert(tabstop_bitmap_prev(bm, 63) == 9);
  assert(tabstop_bitmap_prev(bm, 64) == 64);
  assert(tabstop_bitmap_prev(bm, 65) == 64);
  assert(tabstop_bitmap_prev(bm, 69) == 68);

  assert((bm.bits[0] & 1));
  tabstop_bitmap_set(&bm, 0, 0);
  assert(!(bm.bits[0] & 1));
  tabstop_bitmap_set(&bm, 0, 1);
  assert((bm.bits[0] & 1));

  tabstop_bitmap_set(&bm, 0, 1);

  assert(!(bm.bits[5] & (1 << 25)));
  tabstop_bitmap_set(&bm, 345, 1);
  assert((bm.bits[5] & (1 << 25)));
  tabstop_bitmap_set(&bm, 345, 0);
}

int main(void) {
  test_bitmap();
  test_shmem_allocator();
  test_input_output();
  test_reflow();
  test_erase();
  test_scrolling();
  test_csi_parsing();
  test_string();
  test_base64();
  test_vec();
  test_lua();
  test_lua_modules();
  return n_failures;
}

#include "lauxlib.h"
#include "lua.h"

static void lua_assert(lua_State *L, char *cmd) {
  lua_pushcfunction(L, lua_debug_traceback_handler);
  int handler_idx = lua_gettop(L);

  if (luaL_loadstring(L, cmd) != LUA_OK) {
    lua_die(L);
  }

  if (lua_pcall(L, 0, 0, handler_idx) != LUA_OK) {
    lua_die(L);
  }

  lua_remove(L, handler_idx); // pop the handler
  lua_pop(L, lua_gettop(L));
}

void test_lua() {
  struct velvet v = {.event_loop = io_default};
  velvet_lua_init(&v);
  lua_State *L = v.L;

  char *requires[] = {
      "require('velvet')",                 /* lua/velvet/init.lua */
      "require('velvet.default_options')", /* lua/velvet/init.lua */
  };

  for (int i = 0; i < LENGTH(requires); i++) {
    if (luaL_dostring(L, requires[i]) != LUA_OK) {
      lua_die(L);
    }
    lua_pop(L, lua_gettop(L));
  }

  /* test that options are wired up correctly */
  v.scene.theme.palette[4] = (struct color){ .blue = 0, .kind = COLOR_RGB };
  lua_assert(L, "assert(vv.options.theme.blue ~= 0.0)");
  v.scene.theme.palette[4] = (struct color){ .blue = 255, .kind = COLOR_RGB };
  lua_assert(L, "assert(vv.options.theme.blue ~= 1.0)");

  velvet_destroy(&v);
}

void test_lua_modules() {
  struct velvet v = {.event_loop = io_default, .stored_strings = vec(struct velvet_kvp)};
  velvet_lua_init(&v);
  lua_State *L = v.L;

  if (luaL_dostring(L, "require('velvet')") != LUA_OK) {
    lua_die(L);
  }
  lua_pop(L, lua_gettop(L));

  lua_assert(L, "require('velvet.test').run()");
  velvet_destroy(&v);
}
