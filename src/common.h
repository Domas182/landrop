#ifndef LANDROP_COMMON_H
#define LANDROP_COMMON_H

#include <stdint.h>
#include <stddef.h>

#define LANDROP_MAGIC "LFT1"
#define LANDROP_MAGIC_LEN 4

// Protocol:
// [4 bytes magic] [8 bytes filesize be64] [2 bytes filename_len be16] [filename bytes] [file content]

// Returns 0 on success, -1 on error (errno set)
int read_full(int fd, void *buf, size_t n);
int write_full(int fd, const void *buf, size_t n);

// Endian helpers for 64-bit
uint64_t host_to_be64(uint64_t x);
uint64_t be64_to_host(uint64_t x);

// Get basename of a path (does not modify input)
const char *path_basename(const char *path);

// Sanitize a filename into out buffer (replaces '/' with '_', rejects "..")
// Returns 0 on success, -1 on invalid name or overflow.
int sanitize_filename(const char *name, char *out, size_t out_sz);

#endif // LANDROP_COMMON_H

