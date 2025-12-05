#ifndef PLATFORM_H
#define PLATFORM_H

struct platform_winsize {
  int rows, colums, x_pixel, y_pixel;
};

void platform_get_winsize(struct platform_winsize *w);

extern const struct PLATFORM_IMPL {
  bool (*const get_cwd_from_pty)(int pty, char *buffer, int len);
} platform;

char *platform_get_exe_path();

#endif /*  PLATFORM_H */
