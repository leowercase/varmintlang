#include "../mem.h"
#include "../util.h"

#include <assert.h>
#include <string.h>

/*
 * This header file declares methods for a dynamic array of elements type T.
 * https://en.wikipedia.org/wiki/Dynamic_array
 *
 * Much inspiration was taken from Smart Template Containers' impl.
 * https://github.com/stclib/STC/blob/main/docs/vec_api.md
 *
 * Behold, a generic stack structure!
 *
 * T                element type (required)
 * ARR              dynamic array struct (required)
 * CAP              static maximum capacity, assume .data a finite array
 * ALLOC(ptr, size) custom allocator
 * ALLOC_ARGS       custom arguments for functions that invoke ALLOC()
 * ALLOC_PARAMS     custom parameter names (required if ALLOC_ARGS is defined)
 */

#ifndef T
#error Oops! Must define an element type T.
#endif

#ifndef ARR
#error Oops! Must define the array structure ARR.
#endif

#if (defined(ALLOC) || defined(ALLOC_ARGS)) && defined(CAP)
#error Oops! Cannot both specify ALLOC and have a static CAP.
#endif

#ifndef ALLOC
#define ALLOC(ptr, size) allocate(ptr, size)
#endif

#ifndef ALLOC_ARGS
#define ALLOCS(...) (__VA_ARGS__)
#else
#define ALLOCS(...) (ALLOC_ARGS, __VA_ARGS__)
#endif

#if (defined(ALLOC_ARGS) && !defined(ALLOC_PARAMS)) || (!defined(ALLOC_ARGS) && defined(ALLOC_PARAMS))
#error Oops! Must define parameter names matching ALLOC_ARGS, ALLOC_PARAMS
#endif

#ifndef ALLOC_PARAMS
#define CALLS_ALLOC(...) (__VA_ARGS__)
#else
#define CALLS_ALLOC(...) (ALLOC_PARAMS, __VA_ARGS__)
#endif

#define METHOD(name) JOIN(ARR, name)

#ifndef CAP
void METHOD(_adjust_cap)ALLOCS(ARR *dyn_array, size_t required_cap)
{
  if (dyn_array->cap < required_cap) {
    size_t new_cap = grow_cap(dyn_array->cap);

    dyn_array->data = ALLOC(dyn_array->data, new_cap * sizeof(T));
    dyn_array->cap = new_cap;
  }
}
#endif

// Lazily initialize a new dynamic array
static inline
ARR METHOD(_init)(void)
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
// Initialize array and pre-allocate capacity
static inline
ARR METHOD(_with_cap)ALLOCS(size_t cap)
{
  ARR dyn_array = METHOD(_init)();

  METHOD(_adjust_cap)CALLS_ALLOC(&dyn_array, cap);
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
T *METHOD(_push)ALLOCS(ARR *dyn_array, T elem)
{
#ifdef CAP
  assert(dyn_array->len < CAP);
#else
  METHOD(_adjust_cap)CALLS_ALLOC(dyn_array, dyn_array->len + 1);
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
ARR METHOD(_concat)ALLOCS(ARR *head, ARR *tail)
{
  size_t len = head->len + tail->len - 1;
  ARR dyn_array = METHOD(_with_cap)CALLS_ALLOC(len);
  dyn_array.len = len;

  memcpy(dyn_array.data, head->data, head->len);
  memcpy(dyn_array.data + head->len, tail->data, tail->len);

  return dyn_array;
}
#endif

#undef METHOD
#undef CALLS_ALLOC
#undef ALLOCS
#undef ALLOC_PARAMS
#undef ALLOC_ARGS
#undef ALLOC
#undef CAP
#undef ARR
#undef T
