#ifndef LANG_STR_H
#define LANG_STR_H

#include "util.h"

// Guaranteed to have NUL at index len of s
typedef struct {
  const char *s;
  size_t len;
} Str;

// Isn't null terminated, but rather a slice from a larger corpus.
typedef Str StrSlice;

static inline
Str str_new(char *s, size_t len)
{
  Str str = {s, len};
  return str;
}

static inline
Str str_from(const char *s)
{
  Str str = {s, sizeof(s)};
  return str;
}

static const Str NULL_STR = {NULL, 0};

Str str_fmt(const char *fmt, ...);
Str str_concat(Str head, Str tail);
Str str_copy_slice(StrSlice slice);
bool strs_eq(Str a, Str b);

#endif
