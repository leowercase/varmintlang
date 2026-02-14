#ifndef LANG_STR_H
#define LANG_STR_H

#include "util.h"

// "The most expensive one byte mistake"

// Guaranteed to have NUL at index len of s
typedef struct {
  const char *s;
  size_t len;
} Str;

// Isn't null terminated, but rather a slice from a larger corpus.
typedef Str StrSlice;

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
Str str_fmt(const char *fmt, ...);
Str str_concat(Str head, Str tail);
Str str_copy(Str str);
bool strs_eq(Str a, Str b);

#endif
