#include "platform.h"
#include "utils.h"
#include <termios.h>
#include <unistd.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <virtual_terminal_sequences.h>

static inline void leave_alternate_screen(void);
static inline void enter_alternate_screen(void);
static inline void enable_focus_reporting(void);
static inline void disable_focus_reporting(void);

struct termios original_terminfo;
struct termios raw_term;

static int write_slice(int fd, struct u8_slice slice) { return write(fd, slice.content, slice.len); }

void leave_alternate_screen(void) {
  write_slice(STDOUT_FILENO, vt_leave_alternate_screen);
}

void enter_alternate_screen(void) {
  write_slice(STDOUT_FILENO, vt_enter_alternate_screen);
}

void exit_raw_mode(void) {
  write_slice(STDOUT_FILENO, vt_cursor_visible_on);
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_terminfo);
}

void enable_raw_mode(void) {
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

void platform_get_winsize(struct platform_winsize *w) {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
    die("TIOCGWINSZ:");
  }
  *w = (struct platform_winsize) { .colums = ws.ws_col, .rows = ws.ws_row, .x_pixel = ws.ws_xpixel, .y_pixel = ws.ws_ypixel };
}

static void disable_line_wrapping(void) {
  write_slice(STDOUT_FILENO, vt_line_wrapping_off);
}
static void enable_line_wrapping(void) {
  write_slice(STDOUT_FILENO, vt_line_wrapping_on);
}

static void disable_focus_reporting(void) {
  char buf[] = "\x1b[?1004l";
  write(STDOUT_FILENO, buf, sizeof(buf));
}

static void enable_focus_reporting(void) {
  char buf[] = "\x1b[?1004h";
  write(STDOUT_FILENO, buf, sizeof(buf));
}

static void enable_bracketed_paste(void) {
  char buf[] = "\x1b[?2004h";
  write(STDOUT_FILENO, buf, sizeof(buf));
}

static void disable_bracketed_paste(void) {
  char buf[] = "\x1b[?2004l";
  write(STDOUT_FILENO, buf, sizeof(buf));
}

static void disable_mouse_mode(void) {
  write_slice(STDOUT_FILENO, vt_mouse_mode_sgr_off);
  write_slice(STDOUT_FILENO, vt_mouse_tracking_off);
}

static void enable_mouse_mode(void) {
  write_slice(STDOUT_FILENO, vt_mouse_tracking_on);
  write_slice(STDOUT_FILENO, vt_mouse_mode_sgr_on);
}

void terminal_setup(void) {
  enter_alternate_screen();
  enable_raw_mode();
  disable_line_wrapping();
  enable_focus_reporting();
  enable_bracketed_paste();
  enable_mouse_mode();
}
void terminal_reset(void) {
  disable_mouse_mode();
  disable_bracketed_paste();
  disable_focus_reporting();
  enable_line_wrapping();
  exit_raw_mode();
  leave_alternate_screen();
}

void set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL);
  if (flags == -1 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    die("fcntl:");
  }
}
