#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define LENGTH(x) (sizeof(x) / sizeof((x)[0]))

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define LINE_STR TOSTRING(__LINE__)

#ifdef NDEBUG
#define assert(cond) ((void)0)
#define TODO(...) ((void)0)
#define OMITTED(...) ((void)0)
#else
#define assert(cond)                                                           \
  ((cond) ? (void)0                                                            \
          : (disable_raw_mode_etc(),                                           \
             logmsg("Assertion failed: %s, file %s, line %d\n", #cond,         \
                    __FILE__, __LINE__),                                       \
             exit(EXIT_FAILURE), (void)0))
#define TODO(...)                                                              \
  logmsg("[" __FILE__ ":" LINE_STR "] "                                        \
         "TODO: " __VA_ARGS__)

#define OMITTED(...)                                                           \
  logmsg("[" __FILE__ ":" LINE_STR "] "                                        \
         "OMITTED: " __VA_ARGS__)
#endif

void logmsg(char *fmt, ...);
void flogmsg(FILE *f, char *fmt, ...);
_Noreturn void die(char *fmt, ...);
void *ecalloc(size_t sz, size_t count);
void *erealloc(void *array, size_t nmemb, size_t size);

void enable_raw_mode_etc(void);
void disable_raw_mode_etc(void);
void set_nonblocking(int fd);

extern struct winsize ws_current;

#endif /*  UTILS_H */
