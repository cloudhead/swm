#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

#define MAX_ENV_SIZE 1024

/* Print an error and terminate the process. */
[[noreturn]] void die(const char *fmt, ...) {
    va_list ap;

    fputs("swm: ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    if (fmt[0] && fmt[strlen(fmt) - 1] == ':') {
        fputc(' ', stderr);
        perror(nullptr);
    } else {
        fputc('\n', stderr);
    }
    exit(1);
}

/* Expand environment variables while preserving a single argv element. */
size_t env_expand(char *output, size_t capacity, const char *input) {
    const char *value;
    size_t      i = 0, len, out = 0, start;
    char        name[MAX_ENV_SIZE];

    while (input[i]) {
        if (input[i] != '$') {
            if (out + 1 >= capacity)
                return 0;
            output[out++] = input[i];
            i++;
            continue;
        }
        start = ++i;
        if (!((input[i] >= 'A' && input[i] <= 'Z') || (input[i] >= 'a' && input[i] <= 'z') ||
              input[i] == '_')) {
            if (out + 1 >= capacity)
                return 0;
            output[out++] = '$';
            continue;
        }
        for (; (input[i] >= 'A' && input[i] <= 'Z') || (input[i] >= 'a' && input[i] <= 'z') ||
               (input[i] >= '0' && input[i] <= '9') || input[i] == '_';
             i++)
            ;

        len = i - start;
        if (len >= sizeof(name))
            return 0;
        memcpy(name, input + start, len);
        name[len] = '\0';
        value     = getenv(name);
        if (!value)
            continue;

        len = strlen(value);
        if (out + len >= capacity)
            return 0;
        memcpy(output + out, value, len);
        out += len;
    }
    output[out] = '\0';

    return out + 1;
}

/* Put a file descriptor into nonblocking mode. */
int fd_set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL);

    if (flags < 0) {
        perror("fcntl(F_GETFL)");
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("fcntl(F_SETFL)");
        return -1;
    }
    return 0;
}
