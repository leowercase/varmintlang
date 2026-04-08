#ifndef VARMINT_STR_H
#define VARMINT_STR_H

#include "util.h"

#include <string.h>
// https://github.com/Cyan4973/xxHash
#include <xxhash.h>

// Lightweight length-prefixed string.
// Not garbage collected and does not own the data it points to.
typedef struct {
  const char *s;
  size_t len;
} Str;

static inline
Str str_new(const char *s, const size_t len)
{
  const Str str = {s, len};
  return str;
}

// Create a Str from a string literal.
#define str_from(s) (Str){s, sizeof(s) / sizeof(char) - 1}

static const Str NULL_STR = {NULL, 0};

// Return the 64-bit hash of a string.
static inline
uint64_t str_hash(Str str)
{
  return XXH3_64bits(str.s, str.len);
}

static inline
bool strs_eq(Str a, Str b)
{
  if (a.len != b.len)
    return false;

  return memcmp(a.s, b.s, a.len) == 0;
}

#endif
