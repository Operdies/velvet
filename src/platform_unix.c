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
  write_slice(STDOUT_FILENO, vt_show_cursor);
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

void platform_get_winsize(int *rows, int *columns) {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
    die("TIOCGWINSZ:");
  }
  *rows = ws.ws_row;
  *columns = ws.ws_col;
}

static void disable_line_wrapping(void) {
  write_slice(STDOUT_FILENO, vt_disable_line_wrapping);
}
static void enable_line_wrapping(void) {
  write_slice(STDOUT_FILENO, vt_enable_line_wrapping);
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

void terminal_setup(void) {
  enter_alternate_screen();
  enable_raw_mode();
  disable_line_wrapping();
  enable_focus_reporting();
  enable_bracketed_paste();
}
void terminal_reset(void) {
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
