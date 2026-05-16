#include "../inc/mem.h"

void *allocate(void *ptr, size_t size)
{
  void *new_ptr = realloc(ptr, size);

  if (new_ptr == NULL) exit(EX_OSERR);
  return new_ptr;
}

// https://graphics.stanford.edu/%7Eseander/bithacks.html#RoundUpPowerOf2
static inline size_t round_up_to_pow_2(size_t n)
{
  n--;
  n |= n >> 1;
  n |= n >> 2;
  n |= n >> 4;
  n |= n >> 8;
  n |= n >> 16;
#if SIZE_MAX > UINT32_MAX
  n |= n >> 32;
#endif
#if defined(UINT64_MAX) && SIZE_MAX > UINT64_MAX
  n |= n >> 64;
#endif
  n++;

  return n;
}

size_t cap_to(size_t required_cap)
{
  return required_cap < 8 ? 8 : round_up_to_pow_2(required_cap);
}
