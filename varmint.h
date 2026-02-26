#ifndef LANG_HEART_H
#define LANG_HEART_H

#include "gc.h"
#include "val.h"
#include "state.h"

/*
 * Heart of a Varmint, offspring of the C cockroach.
 * Contains all common state.
 */
typedef struct Varmint {
  CallStack call_stack;
  CallFrame *frame;
  OpStack op_stack;

  Natives natives;
  NativesTable natives_table;

  Value *gc_objects; // GC'd values are stored as a singly linked list.
  GCList grey_worklist;
  size_t bytes_allocd, next_gc; // Tally for the next GC sweep
  GCList compiler_roots;
  const uint8_t *gc_resume_ip;

  Value result;
  char *source;
} Varmint;

// Initialize a Varmint instance.
Varmint varmint_start(void);

// Run some code!
Value varmint_run(Varmint *vm, char *source);

// Free the poor beast.
void varmint_free(Varmint *vm);

// Add a native function.
void varmint_add_native(Varmint *vm,
    char *const name, NativeFn fn, size_t arity);

// Controls debug output.
//#define VARMINT_DEBUG

#endif
