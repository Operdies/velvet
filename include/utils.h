#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void velvet_log(char *fmt, ...) __attribute__((format(printf, 1, 2)));
// Abornmal exit
_Noreturn void velvet_die(char *fmt, ...) __attribute__((format(printf, 1, 2)));
// Normal exit
_Noreturn void velvet_fatal(char *fmt, ...) __attribute__((format(printf, 1, 2)));
void *velvet_calloc(size_t sz, size_t count) __attribute__((malloc, alloc_size(1, 2)));
void *velvet_erealloc(void *array, size_t nmemb, size_t size) __attribute__((alloc_size(2, 3)));
void terminal_setup(void);
void terminal_reset(void);
void set_nonblocking(int fd);

#ifdef RELEASE_BUILD

#define FAIL_ASSERT(cond) __builtin_trap();

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
#define UNUSED(...)                                                                                                    \
  GET_MACRO(__VA_ARGS__, UNUSED_7, UNUSED_6, UNUSED_5, UNUSED_4, UNUSED_3, UNUSED_2, UNUSED_1)(__VA_ARGS__)
#define TODO(...) UNUSED(__VA_ARGS__)
#define OMITTED(...) UNUSED(__VA_ARGS__)
#define ERROR(...) velvet_log(__VA_ARGS__)

#else

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define LINE_STR TOSTRING(__LINE__)
#define DBG(...) velvet_log(__VA_ARGS__)
#define INFO(...) velvet_log(__VA_ARGS__)
#define ERROR(...) velvet_log(__VA_ARGS__)
// Use in stubs to indicate what the missing funcionality is, and communicate
// that implementaiton is planned.
#define TODO(...)                                                                                                      \
  DBG("[" __FILE__ ":" LINE_STR "] "                                                                                   \
      "TODO: " __VA_ARGS__)
// Use in stubs to indicate what the missing functionality is, and communicate
// that no implementation is planned.
#define OMITTED(...)                                                                                                   \
  DBG("[" __FILE__ ":" LINE_STR "] "                                                                                   \
      "OMITTED: " __VA_ARGS__)
#define FAIL_ASSERT(cond)                                                                                              \
  terminal_reset();                                                                                                    \
  ERROR("Assertion failed: %s, file %s, line %d\r\n", #cond, __FILE__, __LINE__);                                      \
  __builtin_trap();
#endif /* RELEASE_BUILD */

#define assert(cond)                                                                                                   \
  do {                                                                                                                 \
    if (!(cond)) {                                                                                                     \
      FAIL_ASSERT(cond);                                                                                               \
    }                                                                                                                  \
  } while (0)

#endif /*  UTILS_H */
