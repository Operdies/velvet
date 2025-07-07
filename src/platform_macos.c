#include "platform.h"
#include <libproc.h>
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

const struct PLATFORM_IMPL platform = {
    .get_cwd_from_pty = get_cwd_from_pty,
};
