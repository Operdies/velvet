#include "platform.h"
#include "utils.h"
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <virtual_terminal_sequences.h>
#include "io.h"

bool terminfo_initialized = false;
struct termios original_terminfo = {0};
struct termios raw_term;

typedef void (*setup_func)(void);
struct setup_pair {
  setup_func enable;
  setup_func disable;
};

void platform_get_winsize(struct platform_winsize *w) {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
    die("TIOCGWINSZ:");
  }
  *w = (struct platform_winsize){
      .colums = ws.ws_col, .rows = ws.ws_row, .x_pixel = ws.ws_xpixel, .y_pixel = ws.ws_ypixel};
}

static void disable_alternate_screen(void) {
  io_write(STDOUT_FILENO, vt_leave_alternate_screen);
}

static void enable_alternate_screen(void) {
  io_write(STDOUT_FILENO, vt_enter_alternate_screen);
}

static void disable_raw_mode(void) {
  io_write(STDOUT_FILENO, vt_cursor_visible_on);
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_terminfo);
}

static void enable_raw_mode(void) {
  // Save and configure raw terminal mode
  if (tcgetattr(STDIN_FILENO, &original_terminfo) == -1) {
    die("tcgetattr:");
  }

  raw_term = original_terminfo;
  cfmakeraw(&raw_term);
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw_term) == -1) {
    die("tcsetattr:");
  }
}

static void disable_focus_reporting(void) {
  io_write(STDOUT_FILENO, vt_focus_reporting_off);
}

static void enable_focus_reporting(void) {
  io_write(STDOUT_FILENO, vt_focus_reporting_on);
}

static void enable_bracketed_paste(void) {
  io_write(STDOUT_FILENO, vt_bracketed_paste_on);
}

static void disable_bracketed_paste(void) {
  io_write(STDOUT_FILENO, vt_bracketed_paste_off);
}

static void disable_mouse_mode(void) {
  io_write(STDOUT_FILENO, vt_mouse_mode_sgr_off);
  io_write(STDOUT_FILENO, vt_mouse_tracking_off);
}

static void enable_mouse_mode(void) {
  io_write(STDOUT_FILENO, vt_mouse_tracking_on);
  io_write(STDOUT_FILENO, vt_mouse_mode_sgr_on);
}

static void disable_kitty_keyboard(void) {
  io_write(STDOUT_FILENO, vt_kitty_keyboard_off);
}

static void enable_kitty_keyboard(void) {
  io_write(STDOUT_FILENO, vt_kitty_keyboard_on);
}

#define SETUP(name)                                                                                                    \
  {                                                                                                                    \
      .enable = enable_##name,                                                                                         \
      .disable = disable_##name,                                                                                       \
  }

static const struct setup_pair setup_functions[] = {
    SETUP(alternate_screen),
    SETUP(raw_mode),
    SETUP(focus_reporting),
    SETUP(bracketed_paste),
    SETUP(mouse_mode),
    // SETUP(kitty_keyboard),
};

void terminal_setup(void) {
  for (int i = 0; i < LENGTH(setup_functions); i++) setup_functions[i].enable();
  terminfo_initialized = true;
}

void terminal_reset(void) {
  if (terminfo_initialized)
    for (int i = LENGTH(setup_functions) - 1; i >= 0; i--)  {
      setup_functions[i].disable();
    }
}

void set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL);
  if (flags == -1 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    die("fcntl:");
  }
}
