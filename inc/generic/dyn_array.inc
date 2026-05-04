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
 * T      element type (required)
 * ARR    dynamic array struct (required)
 * CAP    static maximum capacity; assume .data a finite array
 * USE_GC whether to use GC for allocation
 */

#ifndef T
#error Oops! Must define an element type T.
#endif

#ifndef ARR
#error Oops! Must define the array structure ARR.
#endif

#ifndef USE_GC
#define ALLOCS(...) (__VA_ARGS__)
#define CALLS_ALLOC(...) (__VA_ARGS__)
#else
#define ALLOCS(...) (struct Varmint *vm, __VA_ARGS__)
#define CALLS_ALLOC(...) (vm, __VA_ARGS__)
// Forward declaration.
struct Varmint;
void *gc_alloc(struct Varmint *vm, void *ptr, size_t old_size, size_t new_size);
void gc_free(struct Varmint *vm, void *ptr, size_t size);
#endif

#define METHOD(name) JOIN(ARR, name)

#ifndef CAP
static inline
void METHOD(_reserve_cap)ALLOCS(ARR *dyn_array, size_t required_cap)
{
  if (dyn_array->cap < required_cap) {
    size_t new_cap = cap_to(required_cap);

    dyn_array->data =
#ifndef USE_GC
      allocate(dyn_array->data, new_cap * sizeof(T));
#else
      gc_alloc(vm, dyn_array->data,
        dyn_array->cap * sizeof(T), new_cap * sizeof(T));
#endif
    dyn_array->cap = new_cap;
  }
}

static inline
void METHOD(_reserve_len)ALLOCS(ARR *dyn_array, size_t required_len)
{
  METHOD(_reserve_cap)CALLS_ALLOC(dyn_array, required_len);
  dyn_array->len = required_len;
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

  METHOD(_reserve_cap)CALLS_ALLOC(&dyn_array, cap);
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
  METHOD(_reserve_cap)CALLS_ALLOC(dyn_array, dyn_array->len + 1);
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

#ifndef CAP
// Free dynamic array.
static inline
void METHOD(_free)ALLOCS(ARR *dyn_array)
{
#ifndef USE_GC
  free(dyn_array->data);
#else
  gc_free(vm, dyn_array->data, dyn_array->len * sizeof(T));
#endif
}
#endif

#undef METHOD
#undef CALLS_ALLOC
#undef ALLOCS
#undef USE_GC
#undef CAP
#undef ARR
#undef T
