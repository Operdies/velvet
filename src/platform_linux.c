#include "platform.h"
#include "utils.h"
#include <linux/limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

bool get_cwd_from_pty(int pty, char *buffer, int len) {
  int pid = tcgetpgrp(pty);
  if (pid > 0) {
    char path[100];
    snprintf(path, 100, "/proc/%d/cwd", pid);
    int cnt = readlink(path, buffer, len);
    if (cnt >= 0) {
      buffer[cnt] = 0;
      return true;
    }
  }
  return false;
}

char *platform_get_exe_path() {
  char *buf = calloc(PATH_MAX + 1, 1);
  if (!realpath("/proc/self/exe", buf)) velvet_die("realpath:");
  return buf;
}

bool get_process_from_pty(int pty, char *buf, int size) {
  pid_t fgpid = tcgetpgrp(pty);
  if (fgpid < 0) return false;

  char path[256];
  snprintf(path, sizeof(path), "/proc/%d/comm", fgpid);

  FILE *f = fopen(path, "r");
  if (!f) return false;

  if (!fgets(buf, size, f)) {
    fclose(f);
    return -1;
  }

  fclose(f);

  // Strip trailing newline
  buf[strcspn(buf, "\n")] = 0;
  return true;
}

const struct PLATFORM_IMPL platform = {
  .get_cwd_from_pty = get_cwd_from_pty,
    .get_process_from_pty = get_process_from_pty,
};

