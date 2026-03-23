#ifndef VARMINT_MEM_H
#define VARMINT_MEM_H

#include "util.h"

// Helpers for dealing with memory.

// Allocate space, or reallocate existing ptr
void *allocate(void *ptr, size_t size);

// Grow capacity; guaranteed to be base of 2
size_t grow_cap(size_t cap);

// Compute cap that is at least required_cap
size_t cap_to(size_t required_cap);

#endif
