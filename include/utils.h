#ifndef UTILS_H
#define UTILS_H

#include "lua.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void velvet_log(char *fmt, ...) __attribute__((format(printf, 1, 2)));
_Noreturn void lua_die(lua_State *L);
// Abornmal exit
_Noreturn void velvet_die(char *fmt, ...) __attribute__((format(printf, 1, 2)));
// Normal exit
_Noreturn void velvet_fatal(char *fmt, ...) __attribute__((format(printf, 1, 2)));
void *velvet_calloc(size_t nmemb, size_t size) __attribute__((malloc, alloc_size(1, 2)));
void *velvet_erealloc(void *array, size_t nmemb, size_t size) __attribute__((alloc_size(2, 3)));
void terminal_setup(void (*reset)(void));
void terminal_reset(void);
void set_nonblocking(int fd);
void set_cloexec(int fd);

#define mB(x) ((x) << 20)
#define kB(x) ((x) << 10)

#ifdef RELEASE_BUILD

#define FAIL_ASSERT(cond) __builtin_trap();

__attribute__((unused)) static void DISCARD(char *fmt, ...){(void)fmt;}
#define TODO(...) DISCARD(__VA_ARGS__)
#define OMITTED(...) DISCARD(__VA_ARGS__)
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
  ERROR("Assertion failed: %s, file %s, line %d", #cond, __FILE__, __LINE__);                                      \
  __builtin_trap();
#endif /* RELEASE_BUILD */

#define assert(cond)                                                                                                   \
  do {                                                                                                                 \
    if (!(cond)) {                                                                                                     \
      FAIL_ASSERT(cond);                                                                                               \
    }                                                                                                                  \
  } while (0)

#endif /*  UTILS_H */
