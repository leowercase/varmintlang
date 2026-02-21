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

