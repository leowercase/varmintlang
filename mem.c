#include "error.h"
#include "mem.h"

void *allocate(void *ptr, size_t size)
{
  void *new_ptr = realloc(ptr, size);

  if (new_ptr == NULL) {
    error_out("Out of memory\n");
    exit(EX_OSERR);
  }

  return new_ptr;
}

size_t grow_cap(size_t cap)
{
  return cap < 8 ? 8 : (cap * 2);
}

void adjust_array_cap(void **array, const size_t elem_size,
    size_t *cap, size_t required_cap)
{
  if (*cap < required_cap) {
    size_t new_cap = grow_cap(*cap);

    *array = allocate(*array, new_cap * elem_size);
    *cap = new_cap;
  }
}
