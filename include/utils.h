#ifndef UTILS_H
#define UTILS_H

#include <unistd.h>

void logmsg(char *fmt, ...);
_Noreturn void die(char *fmt, ...);
void *ecalloc(size_t sz, size_t count);
void leave_alternate_screen(void);
void enter_alternate_screen(void);
void exit_raw_mode(void);
void enable_raw_mode(void);

extern struct termios original_terminfo;
extern struct termios raw_term;
extern struct winsize ws;


#endif /*  UTILS_H */
