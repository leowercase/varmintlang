#ifndef VARMINT_GARBAGE_H
#define VARMINT_GARBAGE_H

#include "generic/dyn_array.h"
#include "val.h"

typedef DYN_ARRAY_STRUCT(Value) GCList;
#define T Value
#define ARR GCList
#include "generic/dyn_array.inc"

/*
 * The language employs a garbage collector for dynamic values, objects.
 * During GC, we check from the GC "roots" up whether an object is reachable and
 * free any unreachable ones.
 * The tri-color marking scheme is utilized (condemned - grey - safe).
 * https://en.wikipedia.org/wiki/Tracing_garbage_collection#TRI-COLOR
 */
struct Varmint;

void gc_init(struct Varmint *vm); // Initialize GC
void gc_end(struct Varmint *vm); // Free GC

// Allocate bytes for GC.
void *gc_alloc(struct Varmint *vm, void *ptr, size_t old_size, size_t new_size);
void gc_free(struct Varmint *vm, void *ptr, size_t size);

// Record the GC's ownership of some bytes
void gc_own_bytes(struct Varmint *vm, size_t nbytes);

// Create a new GC object.
Value *create_gc_obj(struct Varmint *vm, Typetag type, size_t size);

// Trigger garbage collection.
void gcollect(struct Varmint *vm);

#endif
