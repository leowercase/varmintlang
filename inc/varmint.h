#ifndef VARMINT_H
#define VARMINT_H

#include "gc.h"
#include "val.h"
#include "state.h"

typedef enum {
  VM_A_OK,
  VM_COMPILE_ERR,
  VM_RUNTIME_ERR,
} VarmintStatus;

/*
 * Heart of a Varmint, offspring of the C cockroach.
 * Contains all common state.
 */
typedef struct Varmint {
  CallStack call_stack;
  CallFrame *frame;
  OpStack op_stack;
  // Linked list of upvalues that capture a value on the stack.
  // Sorted so it reflects the order of the stack.
  Upval *open_upvalues;

  Value *gc_objects; // GC'd values are stored as a singly linked list.
  GCList grey_worklist;
  size_t bytes_allocd, next_gc; // Tally for the next GC sweep
  GCList compiler_roots;
  const uint8_t *gc_resume_ip;

  Value result;
  VarmintStatus status;
} Varmint;

// Initialize a Varmint instance.
Varmint varmint_start(void);

// Run some code!
VarmintStatus varmint_run(Varmint *vm, char *source);

// Free the poor beast.
void varmint_free(Varmint *vm);

// Controls debug output.
//#define VARMINT_DEBUG

#endif
