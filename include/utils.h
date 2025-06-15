#ifndef UTILS_H
#define UTILS_H

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

void logmsg(char *fmt, ...);
void die(char *fmt, ...);
void *ecalloc(size_t sz, size_t count);


#endif /*  UTILS_H */
