#ifndef LANG_VAL_H
#define LANG_VAL_H

#include "util.h"

#include <assert.h>
#include <string.h>

typedef float64_t Value;

void print_value(Value value);

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
