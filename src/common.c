#define _POSIX_C_SOURCE 200809L
#include "common.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

int read_full(int fd, void *buf, size_t n) {
    char *p = (char *)buf;
    size_t left = n;
    while (left > 0) {
        ssize_t r = read(fd, p, left);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) {
            errno = EPIPE; // unexpected EOF
            return -1;
        }
        p += r;
        left -= (size_t)r;
    }
    return 0;
}

int write_full(int fd, const void *buf, size_t n) {
    const char *p = (const char *)buf;
    size_t left = n;
    while (left > 0) {
        ssize_t r = write(fd, p, left);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        p += r;
        left -= (size_t)r;
    }
    return 0;
}

uint64_t host_to_be64(uint64_t x) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return __builtin_bswap64(x);
#else
    return x;
#endif
}

uint64_t be64_to_host(uint64_t x) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return __builtin_bswap64(x);
#else
    return x;
#endif
}

const char *path_basename(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

int sanitize_filename(const char *name, char *out, size_t out_sz) {
    if (!name || !out || out_sz == 0) {
        errno = EINVAL;
        return -1;
    }
    // reject names with ".." to avoid traversal
    if (strstr(name, "..") != NULL) {
        errno = EINVAL;
        return -1;
    }
    size_t len = strlen(name);
    if (len + 1 > out_sz) {
        errno = ENAMETOOLONG;
        return -1;
    }
    for (size_t i = 0; i < len; ++i) {
        char c = name[i];
        if (c == '/' || c == '\\') c = '_';
        out[i] = c;
    }
    out[len] = '\0';
    return 0;
}
