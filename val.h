#ifndef LANG_VAL_H
#define LANG_VAL_H

#include "util.h"

#include <assert.h>
#include <string.h>

typedef enum {
  VAL_NUMBER,
  VAL_BOOL,
} ValueType;

typedef struct {
  ValueType type;
  union {
    float64_t number;
    bool boolean;
  } raw;
} Value;

typedef struct {
  const char *s;
  size_t len;
} Str;

inline Str str_from(const char *s)
{
  Str str = {s, strlen(s)};
  return str;
}

#endif
