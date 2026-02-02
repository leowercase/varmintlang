#ifndef LANG_MEM_H
#define LANG_MEM_H

#include "util.h"

void *allocate(void *ptr, size_t size);

// Grows capacity, guaranteed to be base of 2
size_t grow_cap(size_t cap);

void adjust_array_cap(void **array, const size_t elem_size,
    size_t *cap, size_t required_cap);

#endif
