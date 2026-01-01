#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>
#include "screen.h"

void platform_get_winsize(struct rect *w);
uint64_t get_ms_since_startup(void);

extern const struct PLATFORM_IMPL {
  bool (*const get_cwd_from_pty)(int pty, char *buffer, int len);
} platform;

char *platform_get_exe_path();

#endif /*  PLATFORM_H */
