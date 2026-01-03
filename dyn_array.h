#ifndef LANG_DYN_ARRAY_H
#define LANG_DYN_ARRAY_H

#include "mem.h"
#include "util.h"

#endif

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

#ifndef TYPE_NAME
#define TYPE_NAME JOIN(DynamicArray_, T)
#endif

#define METHOD(name) JOIN(TYPE_NAME, name)

typedef struct {
  size_t len, cap;
  T *data;
} TYPE_NAME;

// Create a new dynamic array
static inline
TYPE_NAME METHOD(_new)()
{
  TYPE_NAME dyn_array;
  dyn_array.len = dyn_array.cap = 0;
  dyn_array.data = NULL;
  return dyn_array;
}

// Append an element
static inline
void METHOD(_push)(TYPE_NAME *dyn_array, T elem)
{
  adjust_array_cap((void **)&dyn_array->data, sizeof(T),
      &dyn_array->cap, dyn_array->len + 1);

  dyn_array->data[dyn_array->len++] = elem;
}

// Pop an element
static inline
T METHOD(_pop)(TYPE_NAME *dyn_array)
{
  return dyn_array->data[--dyn_array->len];
}

// Get the top element
static inline
T METHOD(_top)(TYPE_NAME *dyn_array)
{
  return dyn_array->data[--dyn_array->len];
}

#undef METHOD
#undef TYPE_NAME
#undef T
