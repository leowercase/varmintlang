#ifndef VARMINT_DYN_ARRAY_H
#define VARMINT_DYN_ARRAY_H

#include <stddef.h>

#define DYN_ARRAY(T) \
  size_t len, cap; \
  T *data;

#define DYN_ARRAY_STRUCT(T) \
  struct { DYN_ARRAY(T) }

#define CAPPED_DYN_ARRAY(T, static_cap) \
  size_t len; \
  T data[static_cap];

#define CAPPED_DYN_ARRAY_STRUCT(T, static_cap) \
  struct { CAPPED_DYN_ARRAY(T, static_cap) }

#endif
