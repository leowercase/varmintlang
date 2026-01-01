#ifndef LANG_MEM_H
#define LANG_MEM_H

#include "util.h"

size_t grow_cap(size_t cap);

void *adjust_array_cap(void *array, const size_t elem_size,
    size_t cap, size_t required_cap);

#endif
