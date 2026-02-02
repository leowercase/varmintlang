#ifndef LANG_DYN_ARRAY_HEADER_H
#define LANG_DYN_ARRAY_HEADER_H

#include <stddef.h>

#define DYN_ARRAY(T) \
  size_t len, cap; \
  T *data;

#define DYN_ARRAY_STRUCT(T) \
  struct { DYN_ARRAY(T) }

#endif
