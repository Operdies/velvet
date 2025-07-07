#include "platform.h"
#include <stdio.h>
#include <unistd.h>

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

const struct PLATFORM_IMPL platform = {
  .get_cwd_from_pty = get_cwd_from_pty,
};

