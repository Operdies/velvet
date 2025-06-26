#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <unistd.h>

#define LENGTH(x) (sizeof(x) / sizeof((x)[0]))

void logmsg(char *fmt, ...);
void flogmsg(FILE *f, char *fmt, ...);
_Noreturn void die(char *fmt, ...);
void *ecalloc(size_t sz, size_t count);

void enable_raw_mode_etc(void);
void disable_raw_mode_etc(void);
void set_nonblocking(int fd);

extern struct winsize ws_current;

#endif /*  UTILS_H */
