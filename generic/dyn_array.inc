#include "../mem.h"
#include "../util.h"

#include <assert.h>
#include <string.h>

/*
 * This header file declares a dynamic array of elements of type T.
 * https://en.wikipedia.org/wiki/Dynamic_array
 *
 * Much inspiration was taken from Smart Template Containers' impl.
 * https://github.com/stclib/STC/blob/main/docs/vec_api.md
 *
 * Behold, a generic stack structure!
 */

#ifndef T
#error Oops! Must define an element type T.
#endif

#ifndef ARR
#error Oops! Must define the array structure ARR.
#endif

/* #define CAP maximum static cap */

#define METHOD(name) JOIN(ARR, name)

// Lazily initialize a new dynamic array
static inline
ARR METHOD(_new)(void)
{
  ARR dyn_array;
  dyn_array.len = 0;

#ifndef CAP
  dyn_array.cap = 0;
  dyn_array.data = NULL;
#endif

  return dyn_array;
}

#ifndef CAP
static inline
ARR METHOD(_with_cap)(size_t cap)
{
  ARR dyn_array;
  dyn_array.len = dyn_array.cap = 0;
  dyn_array.data = NULL;

  adjust_array_cap((void **)&dyn_array.data, sizeof(T),
      &dyn_array.cap, cap);

  return dyn_array;
}
#endif

// Get the top element
static inline
T *METHOD(_top)(ARR *dyn_array)
{
  assert(dyn_array->len > 0);
  return &dyn_array->data[dyn_array->len - 1];
}

// Append an element
static inline
T *METHOD(_push)(ARR *dyn_array, T elem)
{
#ifdef CAP
  assert(dyn_array->len < CAP);
#else
  adjust_array_cap((void **)&dyn_array->data, sizeof(T),
      &dyn_array->cap, dyn_array->len + 1);
#endif

  dyn_array->data[dyn_array->len++] = elem;
  return METHOD(_top)(dyn_array);
}

// Pop an element off the top
static inline
T METHOD(_pop)(ARR *dyn_array)
{
  assert(dyn_array->len > 0);
  return dyn_array->data[--dyn_array->len];
}

// Pop n elements off the top
static inline
void METHOD(_popn)(ARR *dyn_array, size_t n)
{
  assert(dyn_array->len >= n);
  dyn_array->len -= n;
}

#ifndef CAP
// Concatenate two dynamic arrays
static inline
ARR METHOD(_concat)(ARR *head, ARR *tail)
{
  size_t len = head->len + tail->len - 1;
  ARR dyn_array = METHOD(_with_cap)(len);
  dyn_array.len = len;

  memcpy(dyn_array.data, head->data, head->len);
  memcpy(dyn_array.data + head->len, tail->data, tail->len);

  return dyn_array;
}
#endif

#undef METHOD
#undef CAP
#undef ARR
#undef T
