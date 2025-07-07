#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void logmsg(char *fmt, ...);
void flogmsg(FILE *f, char *fmt, ...);
_Noreturn void die(char *fmt, ...);
void *ecalloc(size_t sz, size_t count);
void *erealloc(void *array, size_t nmemb, size_t size);
void enable_raw_mode_etc(void);
void disable_raw_mode_etc(void);
void set_nonblocking(int fd);
extern struct winsize ws_current;

#define LENGTH(x) (sizeof(x) / sizeof((x)[0]))


#ifdef NDEBUG

// Experimental performance optimization?
// Mark assertions as unreachable on the assumption that this would have been
// caught during development. Can lead to some extra spicy bugs because failed
// asserts are now undefined behavior instead of an unhandled edge case
#ifdef ASSERTS_UNREACHABLE
#define FAIL_ASSERT(cond) __builtin_unreachable();
#else
#define FAIL_ASSERT(cond) (void)(cond);
#endif

// Mark variables used in logging macros as unused in release builds to silence
// bogus warnings but still keep warnings for legitimately unued variables
#define UNUSED_1(X) (void)(X)
#define UNUSED_2(X, ...) (void)(X), UNUSED_1(__VA_ARGS__)
#define UNUSED_3(X, ...) (void)(X), UNUSED_2(__VA_ARGS__)
#define UNUSED_4(X, ...) (void)(X), UNUSED_3(__VA_ARGS__)
#define UNUSED_5(X, ...) (void)(X), UNUSED_4(__VA_ARGS__)
#define UNUSED_6(X, ...) (void)(X), UNUSED_5(__VA_ARGS__)
#define UNUSED_7(X, ...) (void)(X), UNUSED_6(__VA_ARGS__)
#define GET_MACRO(_1, _2, _3, _4, _5, _6, _7, name, ...) name
#define UNUSED(...)                                                            \
  GET_MACRO(__VA_ARGS__, UNUSED_7, UNUSED_6, UNUSED_5, UNUSED_4, UNUSED_3,     \
            UNUSED_2, UNUSED_1)(__VA_ARGS__)
#define LOG(...) UNUSED(__VA_ARGS__)
#define DIAG(...) UNUSED(__VA_ARGS__)
#define DBG(...) UNUSED(__VA_ARGS__)
#define INFO(...) UNUSED(__VA_ARGS__)
#define TODO(...) UNUSED(__VA_ARGS__)
#define OMITTED(...) UNUSED(__VA_ARGS__)
#define ERROR(...) logmsg(__VA_ARGS__)

#else /* !NDEBUG */

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define LINE_STR TOSTRING(__LINE__)
#define DIAG(...) logmsg(__VA_ARGS__)
#define DBG(...) logmsg(__VA_ARGS__)
#define INFO(...) logmsg(__VA_ARGS__)
#define ERROR(...) logmsg(__VA_ARGS__)
#define TODO(...)                                                              \
  INFO("[" __FILE__ ":" LINE_STR "] "                                          \
       "TODO: " __VA_ARGS__)
#define OMITTED(...)                                                           \
  DIAG("[" __FILE__ ":" LINE_STR "] "                                          \
       "OMITTED: " __VA_ARGS__)
#define FAIL_ASSERT(cond)                                                      \
  disable_raw_mode_etc();                                                      \
  ERROR("Assertion failed: %s, file %s, line %d\n", #cond, __FILE__,           \
        __LINE__);                                                             \
  exit(EXIT_FAILURE);
#endif /* NDEBUG */

#define assert(cond)                                                           \
  do {                                                                         \
    if (!(cond)) {                                                             \
      FAIL_ASSERT(cond);                                                       \
    }                                                                          \
  } while (0)

#endif /*  UTILS_H */
