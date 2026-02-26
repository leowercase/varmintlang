#ifndef LANG_STR_H
#define LANG_STR_H

#include "util.h"

// Lightweight length-prefixed string.
// Not garbage collected.
typedef struct {
  const char *s;
  size_t len;
} Str;

static inline
const Str str_new(const char *s, const size_t len)
{
  const Str str = {s, len};
  return str;
}

// Create a Str from a string literal.
#define str_from(s) (str_new(s, sizeof(s) / sizeof(char) - 1))

static const Str NULL_STR = {NULL, 0};

uint64_t str_hash(Str str);
bool strs_eq(Str a, Str b);

#endif
