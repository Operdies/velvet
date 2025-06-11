#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <util.h>
#include <poll.h>


struct client {
  int pid;
  char process[256];
  int n_buf;
  char buf[];
};

static void spawn(char *path);

static void resize(struct client *client, struct winsize newsize);

int main() {
  struct client c = {0};
  int master_fd;
  struct winsize win = {
      .ws_row = 12, .ws_col = 40, .ws_xpixel = 0, .ws_ypixel = 0};
  struct termios orig_term, raw_term;

  // Get user shell
  struct passwd *pw = getpwuid(getuid());
  const char *shell = pw && pw->pw_shell ? pw->pw_shell : "/bin/sh";

  // Save and configure raw terminal mode
  if (tcgetattr(STDIN_FILENO, &orig_term) == -1) {
    perror("tcgetattr");
    exit(1);
  }

  raw_term = orig_term;
  cfmakeraw(&raw_term); // Use cfmakeraw for proper raw mode
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw_term) == -1) {
    perror("tcsetattr");
    exit(1);
  }

  pid_t pid = forkpty(&master_fd, NULL, NULL, &win);
  if (pid < 0) {
    perror("forkpty");
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_term);
    exit(1);
  }

  if (pid == 0) {
    // In child process
    setenv("TERM", "xterm-256color", 1);
    setenv("SHELL", shell, 1);
    setenv("HOME", pw->pw_dir, 1);
    setenv("USER", pw->pw_name, 1);

    // Login shell (argv[0] prefixed with '-')
    const char *base = strrchr(shell, '/');
    base = base ? base + 1 : shell;

    char login_shell[256];
    snprintf(login_shell, sizeof(login_shell), "-%s", base);

    // execl(shell, login_shell, (char *)NULL);
    execl("/bin/zsh", "zsh", (char *)NULL);
    perror("execl");
    _exit(1);
  }

  // Parent: bridge stdin <-> PTY master <-> stdout
  fd_set fds;
  char buf[512];

  while (1) {
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    FD_SET(master_fd, &fds);

    int maxfd = master_fd > STDIN_FILENO ? master_fd : STDIN_FILENO;

    int ready = select(maxfd + 1, &fds, NULL, NULL, NULL);
    if (ready == -1) {
      if (errno == EINTR)
        continue;
      perror("select");
      break;
    }

    // Input from user → shell
    if (FD_ISSET(STDIN_FILENO, &fds)) {
      ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
      if (n <= 0)
        break;
      write(master_fd, buf, n);
    }

    // Output from shell → user
    if (FD_ISSET(master_fd, &fds)) {
      ssize_t n = read(master_fd, buf, sizeof(buf));
      if (n <= 0)
        break;
      write(STDOUT_FILENO, buf, n);
    }
  }

  // Restore terminal
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_term);
  waitpid(pid, NULL, 0);
  printf("\n[Shell exited]\n");
  return 0;
}
