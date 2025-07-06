#ifndef PLATFORM_H
#define PLATFORM_H


extern const struct PLATFORM_IMPL {
  bool (*const get_cwd_from_pty)(int pty, char *buffer, int len);
} platform;

#endif /*  PLATFORM_H */
