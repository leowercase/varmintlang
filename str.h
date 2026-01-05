#ifndef LANG_STR_H
#define LANG_STR_H

#include "util.h"

typedef struct {
  const char *s;
  size_t len;
} Str;

static inline
Str str_from(const char *s)
{
  Str str = {s, sizeof(s)};
  return str;
}

#endif
