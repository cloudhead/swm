#ifndef SWM_UTIL_H
#define SWM_UTIL_H

#include <stddef.h>

/* Print an error and terminate the process. */
[[noreturn]] void die(const char *fmt, ...);
/* Expand $NAME references without shell evaluation, returning bytes written or zero. */
size_t env_expand(char *output, size_t capacity, const char *input);
/* Put a file descriptor into nonblocking mode. */
int fd_set_nonblock(int fd);

#endif
