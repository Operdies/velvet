#include "platform.h"
#include "utils.h"
#include <linux/limits.h>
#include <stdint.h>

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
  if (!realpath("/proc/self/exe", buf)) die("realpath:");
  return buf;
}


const struct PLATFORM_IMPL platform = {
  .get_cwd_from_pty = get_cwd_from_pty,
};

