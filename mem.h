#ifndef LANG_MEM_H
#define LANG_MEM_H

#include "util.h"

// Helpers for dealing with memory.

// Allocate space, or reallocate existing ptr
void *allocate(void *ptr, size_t size);

// Grow capacity; guaranteed to be base of 2
size_t grow_cap(size_t cap);

#endif
