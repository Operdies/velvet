#include "platform.h"
#include <libproc.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

bool get_cwd_from_pty(int pty, char *buffer, int len) {
  int pid = tcgetpgrp(pty);
  if (pid > 0) {
    struct proc_vnodepathinfo vpi = {0};
    int status = proc_pidinfo(pid, PROC_PIDVNODEPATHINFO, 0, &vpi, PROC_PIDVNODEPATHINFO_SIZE);
    if (status > 0) {
      strncpy(buffer, vpi.pvi_cdir.vip_path, len);
      return true;
    }
  }
  return false;
}

bool _NSGetExecutablePath(char *, uint32_t*);
char *platform_get_exe_path() {
  char *buf1 = calloc(PATH_MAX + 1, 1);
  char *buf2 = calloc(PATH_MAX + 1, 1);
  uint32_t sz = PATH_MAX;
  _NSGetExecutablePath(buf1, &sz);
  realpath(buf1, buf2);
  free(buf1);
  return buf2;
}

const struct PLATFORM_IMPL platform = {
    .get_cwd_from_pty = get_cwd_from_pty,
};
