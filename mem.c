#include "util.h"

size_t grow_cap(size_t cap)
{
  return cap < 8 ? 8 : (cap * 2);
}

void *adjust_array_cap(void *array, const size_t elem_size,
    size_t cap, size_t required_cap)
{
  if (cap >= required_cap)
    return array;

  size_t new_cap = grow_cap(cap);

  void *new_array = realloc(array, new_cap * elem_size);
  if (new_array == NULL) {
    error_out("Out of memory\n");
    exit(EX_OSERR);
  }
  return new_array;
}
