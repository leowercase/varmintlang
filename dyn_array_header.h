#define DYN_ARRAY(T) \
  size_t len, cap; \
  T *data;

#define DYN_ARRAY_STRUCT(T) \
  struct { DYN_ARRAY(T) }
