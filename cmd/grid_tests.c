#include "collections.h"
#include "csi.h"
#include "velvet_scene.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

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
#define RM(x) CSI #x "l"
#define DECSET(x) CSI "?" #x "h"
#define DECRST(x) CSI "?" #x "l"
#define RI "\x1bM"
#define IND                                                                                                            \
  "\x1b"                                                                                                               \
  "D"
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

static void
dumb_screen_print_diff(struct dumb_screen *left, struct dumb_screen *right, char *leftcolor, char *rightcolor) {
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
        printf("%s", color);
        print_codepoint(left->cells[row * left->cols + col]);
        printf("%s", reset);
      } else
        printf("%s%s%s", color, filled, reset);
    }
    printf("│%*s│", align - 2, "");
    for (int col = 0; col < right->cols; col++) {
      bool same = left->cells[row * left->cols + col] == right->cells[row * left->cols + col];
      char *color = same ? reset : rightcolor;
      if (same || right->cells[row * left->cols + col] != ' ') {
        printf("%s", color);
        print_codepoint(right->cells[row * left->cols + col]);
        printf("%s", reset);
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
static void fail(void) {
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
                               {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'},
                               {'i', 'j', 'k'},
                           });
  // move cursor to extremes and type
  test_screen_input_output("cursor movement",
                           CUU(123) CUB(123) CUF(1) CUD(1) "12" CUF(99) CUD(99) CUU(1) CUB(1) "3",
                           (screen_5x8){
                               {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '},
                               {' ', '1', '2', ' ', ' ', ' ', ' ', ' '},
                               {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '},
                               {' ', ' ', ' ', ' ', ' ', ' ', '3', ' '},
                               {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '},
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
                               {' ', ' ', ' ', ' ', ' ', ' ', '3', ' '},
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
                               {'E', 'E', 'E', 'E', 'E', 'E', 'E', 'E'},
                               {'E', 'E', 'E', 'E', 'E', 'E', 'E', 'E'},
                               {'E', 'E', 'E', 'E', 'E', 'E', 'E', 'E'},
                               {'E', 'E', 'E', 'E', 'E', 'E', 'E', 'E'},
                               {'E', 'E', 'E', 'E', 'E', 'E', 'E', 'E'},
                           });
  test_screen_input_output("Clear Command",
                           "\x1b#8" ED(2),
                           (screen_5x8){
                               {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '},
                               {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '},
                               {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '},
                               {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '},
                               {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '},
                           });
  test_screen_input_output(
      "Clear Command 2",
      "wwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwww" ED(2),
      (screen_5x8){
          {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '},
          {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '},
          {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '},
          {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '},
          {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '},
      });
  test_screen_input_output("Off by one",
                           "AAAAAAAABBBBBBBBCCCCCCCCDDDDDDDDEEEEEEEE" SGR(0),
                           (screen_5x8){
                               {'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A'},
                               {'B', 'B', 'B', 'B', 'B', 'B', 'B', 'B'},
                               {'C', 'C', 'C', 'C', 'C', 'C', 'C', 'C'},
                               {'D', 'D', 'D', 'D', 'D', 'D', 'D', 'D'},
                               {'E', 'E', 'E', 'E', 'E', 'E', 'E', 'E'},
                           });
  test_screen_input_output("Off by one 2",
                           CUF(99) CUD(99) SYM(Y) SGR(0) CUU(99) CUB(99) "X" SGR(0),
                           (screen_5x8){
                               {'X', ' ', ' ', ' ', ' ', ' ', ' ', ' '},
                               {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '},
                               {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '},
                               {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '},
                               {' ', ' ', ' ', ' ', ' ', ' ', ' ', 'Y'},
                           });
  test_screen_input_output("Normal Rendering",
                           "Hello!\r\n" EL(0) "Second line\r\n" EL(0) "Third line\r\n" EL(0),
                           (screen_5x8){
                               {'S', 'e', 'c', 'o', 'n', 'd', ' ', 'l'},
                               {'i', 'n', 'e', ' ', ' ', ' ', ' ', ' '},
                               {'T', 'h', 'i', 'r', 'd', ' ', 'l', 'i'},
                               {'n', 'e', ' ', ' ', ' ', ' ', ' ', ' '},
                           });
  test_screen_input_output("Carriage Return",
                           "Hello!!\rworld",
                           (screen_5x8){
                               {'w', 'o', 'r', 'l', 'd', '!', '!'},
                           });

  test_screen_input_output("Insert Lines",
                           SM(20) "Line1\nLine2\nLine3\nLine4\nLine5" CUP(2, 1) IL(2),
                           (screen_5x8){
                               {'L', 'i', 'n', 'e', '1'},
                               {0},
                               {0},
                               {'L', 'i', 'n', 'e', '2'},
                               {'L', 'i', 'n', 'e', '3'},
                           });
  test_screen_input_output("Insert Lines Virtual",
                           SM(20) "Line1\nLine2\nLine3\nLine4\nLine5\nLine6\nLine7" CUP(2, 1) IL(2),
                           (screen_5x8){
                               {'L', 'i', 'n', 'e', '3'},
                               {0},
                               {0},
                               {'L', 'i', 'n', 'e', '4'},
                               {'L', 'i', 'n', 'e', '5'},
                           });
  test_screen_input_output("Delete Lines",
                           SM(20) "Line1\nLine2\nLine3\nLine4\nLine5\nLine6" CUP(2, 1) DL(2),
                           (screen_5x8){
                               {'L', 'i', 'n', 'e', '2'},
                               {'L', 'i', 'n', 'e', '5'},
                               {'L', 'i', 'n', 'e', '6'},
                           });
  test_screen_input_output("Delete Many Lines",
                           SM(20) "Line1\nLine2\nLine3\nLine4\nLine5\nLine6" CUP(2, 1) DL(10),
                           (screen_5x8){
                               {'L', 'i', 'n', 'e', '2'},
                           });
  test_screen_input_output("Delete Many Lines 2",
                           SM(20) "Line1\nLine2\nLine3\nLine4\nLine5\nLine6" CUP(9, 1) DL(10),
                           (screen_5x8){
                               {'L', 'i', 'n', 'e', '2'},
                               {'L', 'i', 'n', 'e', '3'},
                               {'L', 'i', 'n', 'e', '4'},
                               {'L', 'i', 'n', 'e', '5'},
                               {0},
                           });
  test_screen_input_output("Delete Lines All But Last",
                           SM(20) "Line1\nLine2\nLine3\nLine4\nLine5\nLine6" CUP(1, 2) DL(4),
                           (screen_5x8){
                               {'L', 'i', 'n', 'e', '6'},
                           });
  test_screen_input_output("Insert Lines Then Delete",
                           SM(20) "Line1\nLine2\nLine3\nLine4\nLine5\nLine6" CUP(2, 1) DL(2) IL(1),
                           (screen_5x8){
                               {'L', 'i', 'n', 'e', '2'},
                               {0},
                               {'L', 'i', 'n', 'e', '5'},
                               {'L', 'i', 'n', 'e', '6'},
                           });
  test_screen_input_output("Delete Lines Then Insert",
                           SM(20) "Line1\nLine2\nLine3\nLine4\nLine5\nLine6" CUP(2, 1) IL(2) DL(1),
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
                               {'B', 'B', 'B', 'B', 'B', 'B', 'B', 'B'},
                               {'C', 'C', 'C', 'C', 'C', 'C', 'C', 'C'},
                               {'D', 'D', 'D', 'D', 'D', 'D', 'D', 'D'},
                               {'E', 'E', 'E', 'E', 'E', 'E', 'E', 'E'},
                               {'E'},
                           });
  test_screen_input_output("Fill screen",
                           "AAAAAAAA"
                           "BBBBBBBB"
                           "CCCCCCCC"
                           "DDDDDDDD"
                           "EEEEEEEE",
                           (screen_5x8){
                               {'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A'},
                               {'B', 'B', 'B', 'B', 'B', 'B', 'B', 'B'},
                               {'C', 'C', 'C', 'C', 'C', 'C', 'C', 'C'},
                               {'D', 'D', 'D', 'D', 'D', 'D', 'D', 'D'},
                               {'E', 'E', 'E', 'E', 'E', 'E', 'E', 'E'},
                           });
}

static void test_reflow(void) {
  test_screen_reflow_grow("line lengths",
                          "AAAAA"
                          "BBBBB"
                          "CCCCC"
                          "DDDDD",
                          (screen_5x5){
                              {'A', 'A', 'A', 'A', 'A'},
                              {'B', 'B', 'B', 'B', 'B'},
                              {'C', 'C', 'C', 'C', 'C'},
                              {'D', 'D', 'D', 'D', 'D'},
                              {' ', ' ', ' ', ' ', ' '},
                          },
                          (screen_5x8){
                              {'A', 'A', 'A', 'A', 'A', 'B', 'B', 'B'},
                              {'B', 'B', 'C', 'C', 'C', 'C', 'C', 'D'},
                              {'D', 'D', 'D', 'D', ' ', ' ', ' ', ' '},
                          });
  test_screen_reflow_shrink("shrink screen",
                            "AAAAAAAA"
                            "BBBBBBBB"
                            "CCCCCCCC"
                            "DDDDDDDD"
                            "EEEEEEEE",
                            (screen_5x8){
                                {'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A'},
                                {'B', 'B', 'B', 'B', 'B', 'B', 'B', 'B'},
                                {'C', 'C', 'C', 'C', 'C', 'C', 'C', 'C'},
                                {'D', 'D', 'D', 'D', 'D', 'D', 'D', 'D'},
                                {'E', 'E', 'E', 'E', 'E', 'E', 'E', 'E'},
                            },
                            // We are displacing 3x5 characters, so we expect losing 8 A's and 7 B's
                            (screen_5x5){
                                {'B', 'C', 'C', 'C', 'C'},
                                {'C', 'C', 'C', 'C', 'D'},
                                {'D', 'D', 'D', 'D', 'D'},
                                {'D', 'D', 'E', 'E', 'E'},
                                {'E', 'E', 'E', 'E', 'E'},
                            });
  test_screen_reflow_shrink("shrink screen 2",
                            "AAAAAAA\r\n"
                            "BB\r\n"
                            "DDDDDDD",
                            (screen_5x8){
                                {'A', 'A', 'A', 'A', 'A', 'A', 'A', ' '},
                                {'B', 'B', ' ', ' ', ' ', ' ', ' ', ' '},
                                {'D', 'D', 'D', 'D', 'D', 'D', 'D'},
                            },
                            (screen_5x5){
                                {'A', 'A', 'A', 'A', 'A'},
                                {'A', 'A', ' ', ' ', ' '},
                                {'B', 'B', ' ', ' ', ' '},
                                {'D', 'D', 'D', 'D', 'D'},
                                {'D', 'D', ' ', ' ', ' '},
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
                           "xxx" CUP(1, 2) EL(1) "\r\n"                             // Delete first two characters
                                                 "xxxx" EL(2) "\r\n"                // Delete line
                                                              "ababab" EL(0) "\r\n" // Delete nothing
                                                                             "ababab" CUB(5)
                                                                                 EL(0), // Delete all but first
                           (screen_5x8){
                               {' ', ' ', 'x', ' ', ' ', ' ', ' ', ' '},
                               {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '},
                               {'a', 'b', 'a', 'b', 'a', 'b', ' ', ' '},
                               {'a', ' ', ' ', ' ', ' ', ' ', ' ', ' '},
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
                               {' ', ' ', ' ', ' ', ' ', ' ', ' ', 'w'},
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
                               {'h', 'e', 'l', 'l', 'o', 'o', 'o', 'o'},
                               {'o', ' ', ' ', ' ', ' ', ' ', ' ', ' '},
                           });
  test_screen_input_output("Insert Blanks 2",
                           "www" CUB(3)
                           /* delete first w */ DCH(1)
                           /* displace last w past end of line */ ICH(7),
                           (screen_5x8){
                               {' ', ' ', ' ', ' ', ' ', ' ', ' ', 'w'},
                           });
  test_screen_input_output("Line Truncated",
                           "abcd\r" ICH(2),
                           (screen_5x8){
                               {' ', ' ', 'a', 'b', 'c', 'd', ' ', ' '},
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
                               {' ', ' ', 'a', 'b', 'c', 'd', ' ', ' '},
                           });
  test_screen_input_output("Reverse Index (RI)",
                           "\x1b#8" CUP(3, 2) RI "xyz" RI RI,
                           (screen_5x8){
                               {EIGHT(' ')},
                               {EIGHT('E')},
                               {'E', 'x', 'y', 'z', 'E', 'E', 'E', 'E'},
                               {EIGHT('E')},
                               {EIGHT('E')},
                           });
  test_screen_input_output("Index (IND)",
                           "\x1b#8" CUP(3, 2) IND "xyz" IND IND IND RI "a",
                           (screen_5x8){
                               {EIGHT('E')},
                               {'E', 'x', 'y', 'z', 'E', 'E', 'E', 'E'},
                               {EIGHT('E')},
                               {' ', ' ', ' ', ' ', 'a', ' ', ' ', ' '},
                               {EIGHT(' ')},
                           });
  test_screen_input_output("Scroll Down 1 (SD)",
                           "\x1b#8" SD(1),
                           (screen_5x8){
                               {EIGHT(' ')},
                               {EIGHT('E')},
                               {EIGHT('E')},
                               {EIGHT('E')},
                               {EIGHT('E')},
                           });
  test_screen_input_output("Scroll Down 2 (SD)",
                           "\x1b#8" SD(3),
                           (screen_5x8){
                               {EIGHT(' ')},
                               {EIGHT(' ')},
                               {EIGHT(' ')},
                               {EIGHT('E')},
                               {EIGHT('E')},
                           });
  test_screen_input_output("Scroll Down + Scroll Region (SD)",
                           DECSTBM(2, 4) "\x1b#8" SD(2),
                           (screen_5x8){
                               {EIGHT('E')},
                               {EIGHT(' ')},
                               {EIGHT(' ')},
                               {EIGHT('E')},
                               {EIGHT('E')},
                           });
  test_screen_input_output("Scroll Up 1 (SU)",
                           "\x1b#8" SU(1),
                           (screen_5x8){
                               {EIGHT('E')},
                               {EIGHT('E')},
                               {EIGHT('E')},
                               {EIGHT('E')},
                               {EIGHT(' ')},
                           });
  test_screen_input_output("Scroll Up 2 (SU)",
                           "\x1b#8" SU(3),
                           (screen_5x8){
                               {EIGHT('E')},
                               {EIGHT('E')},
                               {EIGHT(' ')},
                               {EIGHT(' ')},
                               {EIGHT(' ')},
                           });
  test_screen_input_output("Scroll Up + Scroll Region (SU)",
                           DECSTBM(2, 4) "\x1b#8" SU(2),
                           (screen_5x8){
                               {EIGHT('E')},
                               {EIGHT('E')},
                               {EIGHT(' ')},
                               {EIGHT(' ')},
                               {EIGHT('E')},
                           });
}

static void test_nowrap(void) {
  /* DECRST(7) disables auto-wrap mode (DECAWM) */

  /* ascii: text stops at right margin, last char overwrites */
  test_screen_input_output("nowrap ascii overflow",
                           DECRST(7) "abcdefghijk",
                           (screen_5x8){
                               {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'k'},
                           });

  /* ascii: exact fit */
  test_screen_input_output("nowrap ascii exact fit",
                           DECRST(7) "abcdefgh",
                           (screen_5x8){
                               {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'},
                           });

  /* ascii: short text */
  test_screen_input_output("nowrap ascii short",
                           DECRST(7) "abc",
                           (screen_5x8){
                               {'a', 'b', 'c'},
                           });

  /* ascii: continued overwrite at right edge */
  test_screen_input_output("nowrap ascii overwrite edge",
                           DECRST(7) "abcdefghxyz",
                           (screen_5x8){
                               {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'z'},
                           });

  /* unicode narrow (2-byte utf8): overflow */
  test_screen_input_output("nowrap unicode narrow overflow",
                           DECRST(7) "abcdefgééé",
                           (screen_5x8){
                               {'a', 'b', 'c', 'd', 'e', 'f', 'g', L'é'},
                           });

  /* unicode narrow: exact fit */
  test_screen_input_output("nowrap unicode narrow exact",
                           DECRST(7) "abcdefgé",
                           (screen_5x8){
                               {'a', 'b', 'c', 'd', 'e', 'f', 'g', L'é'},
                           });

  /* mixed ascii and unicode narrow */
  test_screen_input_output("nowrap mixed narrow",
                           DECRST(7) "αβγdefghijk",
                           (screen_5x8){
                               {L'α', L'β', L'γ', 'd', 'e', 'f', 'g', 'k'},
                           });

  /* wide character (CJK, 2 columns): fits */
  test_screen_input_output("nowrap wide fit",
                           DECRST(7) "abc中文",
                           (screen_5x8){
                               {'a', 'b', 'c', L'中', ' ', L'文', ' '},
                           });

  /* wide character: cannot start on last column */
  test_screen_input_output("nowrap wide at edge",
                           DECRST(7) "abcdefg中",
                           (screen_5x8){
                               {'a', 'b', 'c', 'd', 'e', 'f', 'g'},
                           });

  /* re-enable wrap after disabling */
  test_screen_input_output("nowrap then wrap",
                           DECRST(7) "abcdefghijk" DECSET(7) "\r\n"
                                                             "abcdefghijk",
                           (screen_5x8){
                               {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'k'},
                               {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'},
                               {'i', 'j', 'k'},
                           });

  /* newline still works in nowrap mode */
  test_screen_input_output("nowrap with newline",
                           DECRST(7) SM(20) "abcdefghijk\nxyz",
                           (screen_5x8){
                               {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'k'},
                               {'x', 'y', 'z'},
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
      (struct csi){.final = 'm',
                   .n_params = 2,
                   .state = CSI_ACCEPT,
                   .params = {{.primary = 48, .sub = {2, 118, 159, 240}}, {.primary = 38, .sub = {2, 235, 160, 172}}}});
  test_csi_testcase("Test prefix / intermediate parsing 1",
                    ">c",
                    (struct csi){.prefix = '>', .final = 'c', .state = CSI_ACCEPT, .n_params = 0});
  test_csi_testcase("Test prefix / intermediate parsing 1",
                    ">?c",
                    (struct csi){.prefix = '>', .final = 'c', .state = CSI_ACCEPT, .n_params = 0, .intermediate = '?'});
  test_csi_testcase("Test prefix / intermediate parsing 1",
                    "4?c",
                    (struct csi){.prefix = 0,
                                 .final = 'c',
                                 .state = CSI_ACCEPT,
                                 .n_params = 1,
                                 .params = {{.primary = 4}},
                                 .intermediate = '?'});
  test_csi_testcase(
      "Test n_params", " q", (struct csi){.prefix = ' ', .final = 'q', .state = CSI_ACCEPT, .n_params = 0});
}
